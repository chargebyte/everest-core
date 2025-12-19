// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "SeccConfigurationStore.hpp"

#include <algorithm>
#include <cctype>

#include "EvseManager.hpp"
#include "everest/logging.hpp"
#include "utils.hpp"

namespace module {

std::string SeccConfigurationStore::normalize_mac(std::string mac) {
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    std::transform(mac.begin(), mac.end(), mac.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return mac;
}

bool SeccConfigurationStore::mac_matches(const std::string& normalized_mac, const std::string& normalized_filter) {
    return normalized_mac.rfind(normalized_filter, 0) == 0;
}

types::iso15118::SupportedAppProtocols
SeccConfigurationStore::build_requested_protocols(const types::evse_manager::ACChargingSessionConfiguration& config) {
    types::iso15118::SupportedAppProtocols requested;

    if (config.allow_isod2) {
        requested.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118d2);
    }
    if (config.allow_isod20) {
        requested.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118d20);
    }
    if (config.allow_isod2_fake_dc) {
        requested.app_protocols.push_back(types::iso15118::SupportedAppProtocol::DIN70121);

        if (!config.allow_isod2) {
            requested.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118d2);
        }
    }

    return requested;
}

bool SeccConfigurationStore::are_requested_protocols_supported(
    const types::iso15118::SupportedAppProtocols& requested,
    const std::optional<types::iso15118::SupportedAppProtocols>& available_protocols) {
    if (!available_protocols.has_value()) {
        return true;
    }

    for (const auto& req : requested.app_protocols) {
        const auto it =
            std::find(available_protocols->app_protocols.begin(), available_protocols->app_protocols.end(), req);
        if (it == available_protocols->app_protocols.end()) {
            EVLOG_warning << "Requested app protocol " << types::iso15118::supported_app_protocol_to_string(req)
                          << " not supported by SECC";
            return false;
        }
    }

    return true;
}

bool SeccConfigurationStore::set_secc_configuration(const types::evse_manager::ACChargingSessionConfiguration& config) {
    std::scoped_lock lock(mutex);
    if (!module_config) {
        return false;
    }

    if (config.allow_isod2_fake_dc && !module_config->ac_with_soc) {
        EVLOG_error << "Rejecting AC charging session configuration: allow_isod2_fake_dc requested but "
                    << "ac_with_soc is disabled in EVSE config.";
        return false;
    }

    const auto requested = build_requested_protocols(config);
    const auto available = available_app_protocols;

    if (!are_requested_protocols_supported(requested, available)) {
        return false;
    }

    const bool hlc_enabled_for_session =
        module_config->ac_hlc_enabled && (config.allow_isod2 || config.allow_isod20 || config.allow_isod2_fake_dc);
    const auto reinit_method = config.reinit_configuration && config.reinit_configuration->state_transition
                                   ? *config.reinit_configuration->state_transition
                                   : types::evse_manager::string_to_reinit_state_enum(module_config->reinit_method);
    const int reinit_duration = config.reinit_configuration && config.reinit_configuration->duration.has_value()
                                    ? *config.reinit_configuration->duration
                                    : module_config->reinit_duration_ms;
    const auto mode = module_config->charge_mode == "DC" ? SeccConfigurationStore::ChargeMode::DC
                                                         : SeccConfigurationStore::ChargeMode::AC;

    const SeccConfig session_config{module_config->has_ventilation,
                                    mode,
                                    hlc_enabled_for_session,
                                    module_config->ac_hlc_use_5percent,
                                    module_config->ac_slac_reset_attempts,
                                    module_config->ac_enforce_hlc,
                                    config.allow_isod2_fake_dc,
                                    requested,
                                    static_cast<float>(module_config->soft_over_current_tolerance_percent),
                                    static_cast<float>(module_config->soft_over_current_measurement_noise_A),
                                    module_config->switch_3ph1ph_delay_s,
                                    module_config->switch_3ph1ph_cp_state,
                                    module_config->soft_over_current_timeout_ms,
                                    module_config->state_F_after_fault_ms,
                                    module_config->fail_on_powermeter_errors,
                                    module_config->raise_mrec9,
                                    module_config->sleep_before_enabling_pwm_hlc_mode_ms,
                                    utils::get_session_id_type_from_string(module_config->session_id_type),
                                    reinit_duration,
                                    reinit_method};

    // Store session config either scoped to the provided MAC filters or as the default config
    if (config.mac_filter && !config.mac_filter->empty()) {
        for (const auto& mac : *config.mac_filter) {
            const auto normalized_mac = normalize_mac(mac);
            if (!normalized_mac.empty()) {
                mac_scoped_ac_configs[normalized_mac] = session_config;
            }
        }
    } else {
        secc_config = session_config;
    }
    return true;
}

void SeccConfigurationStore::set_available_app_protocols(const types::iso15118::SupportedAppProtocols& protocols) {
    std::string protocols_str;
    for (size_t i = 0; i < protocols.app_protocols.size(); ++i) {
        protocols_str += types::iso15118::supported_app_protocol_to_string(protocols.app_protocols[i]);
        if (i + 1 < protocols.app_protocols.size()) {
            protocols_str += ",";
        }
    }
    EVLOG_info << "Updating available app protocols from SECC: [" << protocols_str << "]";

    std::scoped_lock lock(mutex);
    available_app_protocols = protocols;
    secc_config.supported_app_protocols = protocols;
}

SeccConfigurationStore::SeccConfig SeccConfigurationStore::get_secc_configuration(const std::string& ev_mac) const {
    const auto normalized_mac = normalize_mac(ev_mac);

    std::scoped_lock lock(mutex);

    const SeccConfig* best_match = nullptr;
    std::size_t best_match_len = 0;

    for (const auto& [filter, config] : mac_scoped_ac_configs) {
        if (filter.size() > normalized_mac.size() || filter.size() <= best_match_len) {
            continue;
        }

        if (normalized_mac.compare(0, filter.size(), filter) == 0) {
            best_match = &config;
            best_match_len = filter.size();

            if (best_match_len == normalized_mac.size()) {
                break;
            }
        }
    }

    if (best_match != nullptr) {
        last_secc_configuration = *best_match;
        return *best_match;
    }

    EVLOG_debug << "No MAC-specific SECC configuration found for " << ev_mac << ", using default";
    last_secc_configuration = this->secc_config;
    return this->secc_config;
}

SeccConfigurationStore::SeccConfig SeccConfigurationStore::get_secc_configuration() const {
    std::scoped_lock lock(mutex);
    last_secc_configuration = secc_config;
    return secc_config;
}

std::optional<SeccConfigurationStore::SeccConfig> SeccConfigurationStore::get_last_secc_configuration() const {
    std::scoped_lock lock(mutex);
    return last_secc_configuration;
}

void SeccConfigurationStore::set_secc_configuration(const Conf& config) {
    std::scoped_lock lock(mutex);
    module_config = std::make_shared<Conf>(config);

    const auto mode =
        config.charge_mode == "DC" ? SeccConfigurationStore::ChargeMode::DC : SeccConfigurationStore::ChargeMode::AC;

    const SeccConfig cfg{config.has_ventilation,
                         mode,
                         config.ac_hlc_enabled,
                         config.ac_hlc_use_5percent,
                         config.ac_slac_reset_attempts,
                         config.ac_enforce_hlc,
                         config.ac_with_soc,
                         {},
                         static_cast<float>(config.soft_over_current_tolerance_percent),
                         static_cast<float>(config.soft_over_current_measurement_noise_A),
                         config.switch_3ph1ph_delay_s,
                         config.switch_3ph1ph_cp_state,
                         config.soft_over_current_timeout_ms,
                         config.state_F_after_fault_ms,
                         config.fail_on_powermeter_errors,
                         config.raise_mrec9,
                         config.sleep_before_enabling_pwm_hlc_mode_ms,
                         utils::get_session_id_type_from_string(config.session_id_type),
                         config.reinit_duration_ms,
                         types::evse_manager::string_to_reinit_state_enum(config.reinit_method)};

    secc_config = cfg;
}

bool SeccConfigurationStore::set_secc_configuration(ChargeMode mode, bool ac_hlc_enabled) {
    std::scoped_lock lock(mutex);

    secc_config.charge_mode = mode;
    secc_config.ac_hlc_enabled = ac_hlc_enabled;

    return true;
}

void SeccConfigurationStore::set_current_secc_config(const SeccConfigurationStore::SeccConfig& cfg) {
    std::scoped_lock lock(mutex);
    secc_config = cfg;
}

} // namespace module
