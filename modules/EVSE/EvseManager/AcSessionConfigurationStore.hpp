// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "generated/types/evse_manager.hpp"
#include "generated/types/iso15118.hpp"

namespace module {

class AcSessionConfigurationStore {
public:
    struct Policy {
        types::evse_manager::ACChargingSessionConfiguration configuration;
        bool manifest_default;
    };

    explicit AcSessionConfigurationStore(types::evse_manager::ACChargingSessionConfiguration manifest_default) :
        default_policy{std::move(manifest_default), true} {
    }

    bool set(const types::evse_manager::ACChargingSessionConfiguration& configuration) {
        std::vector<std::string> normalized_filters;
        if (configuration.mac_filter.has_value() && !configuration.mac_filter->empty()) {
            normalized_filters.reserve(configuration.mac_filter->size());
            for (const auto& filter : *configuration.mac_filter) {
                auto normalized = normalize_mac(filter);
                if (!normalized.has_value()) {
                    return false;
                }
                normalized_filters.push_back(std::move(*normalized));
            }
        }

        std::scoped_lock lock(mutex);
        if (available_protocols.has_value() && !supports(configuration, *available_protocols)) {
            return false;
        }
        if (normalized_filters.empty()) {
            default_policy = {configuration, false};
        } else {
            for (const auto& filter : normalized_filters) {
                mac_policies[filter] = {configuration, false};
            }
        }
        return true;
    }

    Policy get(const std::string& mac) const {
        const auto normalized_mac = normalize_mac(mac);
        std::scoped_lock lock(mutex);
        if (!normalized_mac.has_value()) {
            return default_policy;
        }

        const Policy* best_match = nullptr;
        std::size_t best_match_size = 0;
        for (const auto& [filter, policy] : mac_policies) {
            if (filter.size() > best_match_size && normalized_mac->compare(0, filter.size(), filter) == 0) {
                best_match = &policy;
                best_match_size = filter.size();
            }
        }
        return best_match == nullptr ? default_policy : *best_match;
    }

    Policy get_default() const {
        std::scoped_lock lock(mutex);
        return default_policy;
    }

    bool has_mac_rules() const {
        std::scoped_lock lock(mutex);
        return !mac_policies.empty();
    }

    void set_available_protocols(const types::iso15118::SupportedAppProtocols& protocols) {
        std::scoped_lock lock(mutex);
        available_protocols = protocols;
    }

    types::iso15118::SupportedAppProtocols protocols_for(const Policy& policy, bool include_fake_dc = true) const {
        std::optional<types::iso15118::SupportedAppProtocols> available;
        {
            std::scoped_lock lock(mutex);
            available = available_protocols;
        }

        types::iso15118::SupportedAppProtocols protocols;
        const auto is_available = [&available](types::iso15118::SupportedAppProtocol protocol) {
            return !available.has_value() || std::find(available->app_protocols.begin(), available->app_protocols.end(),
                                                       protocol) != available->app_protocols.end();
        };
        if (!include_fake_dc && policy.configuration.allow_isod20 &&
            is_available(types::iso15118::SupportedAppProtocol::ISO15118D20)) {
            protocols.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118D20);
        }
        if (policy.configuration.allow_isod2 && is_available(types::iso15118::SupportedAppProtocol::ISO15118D2)) {
            protocols.app_protocols.push_back(types::iso15118::SupportedAppProtocol::ISO15118D2);
        }
        if (include_fake_dc && policy.configuration.allow_isod2_fake_dc &&
            is_available(types::iso15118::SupportedAppProtocol::DIN70121)) {
            protocols.app_protocols.push_back(types::iso15118::SupportedAppProtocol::DIN70121);
        }
        return protocols;
    }

    static std::optional<std::string> normalize_mac(const std::string& mac) {
        std::string normalized;
        normalized.reserve(mac.size());
        for (const unsigned char character : mac) {
            if (character == ':' || character == '-') {
                continue;
            }
            if (!std::isxdigit(character)) {
                return std::nullopt;
            }
            normalized.push_back(static_cast<char>(std::toupper(character)));
        }
        if (normalized.size() != 6 && normalized.size() != 12) {
            return std::nullopt;
        }
        return normalized;
    }

private:
    static bool supports(const types::evse_manager::ACChargingSessionConfiguration& configuration,
                         const types::iso15118::SupportedAppProtocols& available) {
        const auto has_protocol = [&available](types::iso15118::SupportedAppProtocol protocol) {
            return std::find(available.app_protocols.begin(), available.app_protocols.end(), protocol) !=
                   available.app_protocols.end();
        };
        if (configuration.allow_isod20 && !has_protocol(types::iso15118::SupportedAppProtocol::ISO15118D20)) {
            return false;
        }
        if (configuration.allow_isod2 && !has_protocol(types::iso15118::SupportedAppProtocol::ISO15118D2)) {
            return false;
        }
        return !configuration.allow_isod2_fake_dc || has_protocol(types::iso15118::SupportedAppProtocol::DIN70121) ||
               (configuration.allow_isod2 && has_protocol(types::iso15118::SupportedAppProtocol::ISO15118D2));
    }

    mutable std::mutex mutex;
    Policy default_policy;
    std::map<std::string, Policy> mac_policies;
    std::optional<types::iso15118::SupportedAppProtocols> available_protocols;
};

} // namespace module
