// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SECC_CONFIGURATION_STORE_HPP
#define SECC_CONFIGURATION_STORE_HPP

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "utils.hpp"
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>

namespace module {

struct Conf;

/// Stores general SECC configuration and EV MAC/OUI specific SECC configurations
class SeccConfigurationStore {
public:
    enum class ChargeMode {
        AC,
        DC
    };

    struct SeccConfig {
        bool has_ventilation;
        ChargeMode charge_mode;
        bool ac_hlc_enabled;
        bool ac_hlc_use_5percent;
        int ac_slac_reset_attempts;
        bool ac_enforce_hlc;
        bool allow_isod2_fake_dc;
        types::iso15118::SupportedAppProtocols supported_app_protocols;
        float soft_over_current_tolerance_percent;
        float soft_over_current_measurement_noise_A;
        int switch_3ph1ph_delay_ms;
        types::evse_manager::ReinitStateEnum phase_switch_method;
        int soft_over_current_timeout_ms;
        int state_F_after_fault_ms;
        bool fail_on_powermeter_errors;
        bool raise_mrec9;
        int sleep_before_enabling_pwm_hlc_mode_ms;
        utils::SessionIdType session_id_type;
        int reinit_duration_ms;
        types::evse_manager::ReinitStateEnum reinit_method;
    };

    SeccConfigurationStore() = default;

    /// Stores the default configuration (used when no MAC-specific match exists).
    /// Returns false if requested protocols are not supported by SECC (when available protocols are known).
    bool set_secc_configuration(const types::evse_manager::ACChargingSessionConfiguration& config);

    /// Stores the SECC configuration based on the the charge mode and ac_hlc_enabled flag
    bool set_secc_configuration(ChargeMode mode, bool ac_hlc_enabled);

    /// Stores the SECC configuration based on the Conf struct
    void set_secc_configuration(const Conf& conf);

    /// Stores the SECC configuration
    void set_current_secc_config(const SeccConfig& cfg);

    /// Retrieves the SECC configuration matching the provided EV MAC (full MAC or OUI prefix).
    /// Returns the default configuration if no specific match exists
    SeccConfig get_secc_configuration(const std::string& ev_mac) const;

    /// Returns the stored SECC configuration
    SeccConfig get_secc_configuration() const;

    /// Returns the most recently retrieved SECC configuration, if one was requested; useful after a MAC-scoped lookup.
    std::optional<SeccConfig> get_last_secc_configuration() const;

    /// Helper to derive requested app protocols from AC session configuration.
    static types::iso15118::SupportedAppProtocols
    build_requested_protocols(const types::evse_manager::ACChargingSessionConfiguration& config);

    /// Helper to check requested protocols against available ones (if provided).
    static bool
    are_requested_protocols_supported(const types::iso15118::SupportedAppProtocols& requested,
                                      const std::optional<types::iso15118::SupportedAppProtocols>& available_protocols);

    /// Stores supported app protocols reported by SECC.
    void set_available_app_protocols(const types::iso15118::SupportedAppProtocols& protocols);

private:
    static std::string normalize_mac(std::string mac);
    static bool mac_matches(const std::string& normalized_mac, const std::string& normalized_filter);

    mutable std::mutex mutex;
    std::map<std::string, SeccConfig> mac_scoped_ac_configs;
    mutable std::optional<SeccConfig> last_secc_configuration;
    SeccConfig secc_config;
    std::shared_ptr<Conf> module_config;
    std::optional<types::iso15118::SupportedAppProtocols> available_app_protocols;
};

} // namespace module

#endif // SECC_CONFIGURATION_STORE_HPP
