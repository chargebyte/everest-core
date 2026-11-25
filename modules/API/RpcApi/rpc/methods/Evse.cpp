// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest

#include "Evse.hpp"

#include <exception>
#include <limits>

namespace methods {

RPCDataTypes::EVSEGetInfoResObj Evse::get_info(const int32_t evse_index) {
    RPCDataTypes::EVSEGetInfoResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }

    const auto data = evse->evseinfo.get_data();
    if (not data.has_value()) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorNoDataAvailable;
        return res;
    }

    res.info = data.value();
    res.error = RPCDataTypes::ResponseErrorEnum::NoError;
    return res;
}

RPCDataTypes::EVSEGetStatusResObj Evse::get_status(const int32_t evse_index) {
    RPCDataTypes::EVSEGetStatusResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }

    const auto data = evse->evsestatus.get_data();
    if (not data.has_value()) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorNoDataAvailable;
        return res;
    }

    res.status = data.value();
    res.error = RPCDataTypes::ResponseErrorEnum::NoError;
    return res;
}

RPCDataTypes::EVSEGetHardwareCapabilitiesResObj Evse::get_hardware_capabilities(const int32_t evse_index) {
    RPCDataTypes::EVSEGetHardwareCapabilitiesResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }

    const auto data = evse->hardwarecapabilities.get_data();
    if (not data.has_value()) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorNoDataAvailable;
        return res;
    }

    res.hardware_capabilities = data.value();
    return res;
}

RPCDataTypes::ErrorResObj Evse::set_charging_allowed(const int32_t evse_index, bool charging_allowed) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }
    return m_request_handler_ptr->set_charging_allowed(evse_index, charging_allowed);
}

RPCDataTypes::EVSEGetMeterDataResObj Evse::get_meter_data(const int32_t evse_index) {
    RPCDataTypes::EVSEGetMeterDataResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }

    const auto data = evse->meterdata.get_data();
    if (not data.has_value()) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorNoDataAvailable;
        return res;
    }
    res.meter_data = data.value();
    return res;
}

RPCDataTypes::ErrorResObj Evse::set_ac_charging(const int32_t evse_index, bool charging_allowed, float max_current,
                                                std::optional<int> phase_count) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }
    return m_request_handler_ptr->set_ac_charging(evse_index, charging_allowed, max_current, phase_count);
}

RPCDataTypes::ErrorResObj Evse::set_ac_charging_current(const int32_t evse_index, float max_current) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }
    return m_request_handler_ptr->set_ac_charging_current(evse_index, max_current);
}

RPCDataTypes::ErrorResObj Evse::set_ac_charging_phase_count(const int32_t evse_index, int phase_count) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }

    // Check if the requested phase count is equal to the current active phase count
    // If so, we can return a success response without making any changes. Phase switching is not
    // necessary in this case.
    auto evse_status = evse->evsestatus.get_data();
    if (evse_status.has_value() && evse_status.value().ac_charge_status.has_value()) {
        if (evse_status.value().ac_charge_status.value().evse_active_phase_count == phase_count) {
            res.error = RPCDataTypes::ResponseErrorEnum::NoError;
            return res;
        }
    }

    // If phase switching must be performed and the hardware capabilities do not allow it
    // we return an error response.
    auto hardwarecapabilities = evse->hardwarecapabilities.get_data();
    if (hardwarecapabilities.has_value() && hardwarecapabilities.value().phase_switch_during_charging == false) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorOperationNotSupported;
        return res;
    }

    // Check if the requested phase count is within the allowed range
    if ((hardwarecapabilities.has_value() && phase_count < hardwarecapabilities.value().min_phase_count_export) ||
        phase_count > hardwarecapabilities.value().max_phase_count_export) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorOutOfRange;
        return res;
    }

    // Check if phases are 1 or 3, otherwise return an error
    if (phase_count != 1 && phase_count != 3) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
        return res;
    }

    return m_request_handler_ptr->set_ac_charging_phase_count(evse_index, phase_count);
}

RPCDataTypes::ErrorResObj Evse::set_dc_charging(const int32_t evse_index, bool charging_allowed, float max_power) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }
    return m_request_handler_ptr->set_dc_charging(evse_index, charging_allowed, max_power);
}

RPCDataTypes::ErrorResObj Evse::set_dc_charging_power(const int32_t evse_index, float max_power) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }
    return m_request_handler_ptr->set_dc_charging_power(evse_index, max_power);
}

RPCDataTypes::ErrorResObj Evse::enable_connector(const int32_t evse_index, int connector_index, bool enable,
                                                 int priority) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }
    // Iterate through the connectors to find the one with the given ID
    const auto connectors = evse->evseinfo.get_available_connectors();
    auto it = std::find_if(connectors.begin(), connectors.end(),
                           [connector_index](const auto& connector) { return connector.index == connector_index; });
    // If not found, return an error
    if (it == connectors.end()) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidConnectorIndex;
        return res;
    }
    return m_request_handler_ptr->enable_connector(evse_index, connector_index, enable, priority);
}

RPCDataTypes::ErrorResObj Evse::set_ac_session_configuration(int32_t evse_index, nlohmann::json configuration) {
    if (!m_dataobj.get_evse_store(evse_index)) {
        return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex};
    }
    try {
        if (!configuration.is_object()) {
            return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
        }
        for (const auto& [key, value] : configuration.items()) {
            if (key == "allow_isod20" || key == "allow_isod2" || key == "allow_hlc_fake_dc" ||
                key == "disable_isod2_fake_dc_after_replug") {
                if (!value.is_boolean()) {
                    return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
                }
            } else if (key == "reinit_configuration" || key == "phase_switch_configuration") {
                if (!value.is_object()) {
                    return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
                }
                for (const auto& [field, setting] : value.items()) {
                    if (field == "state_transition") {
                        if (!setting.is_string()) {
                            return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
                        }
                        types::evse_manager::string_to_reinit_state_enum(setting.get<std::string>());
                    } else if (field == "duration") {
                        if ((!setting.is_number_integer() && !setting.is_number_unsigned()) ||
                            (setting.is_number_unsigned() &&
                             setting.get<uint64_t>() > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) ||
                            (!setting.is_number_unsigned() &&
                             (setting.get<int64_t>() < 0 ||
                              setting.get<int64_t>() > std::numeric_limits<int32_t>::max()))) {
                            return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
                        }
                    } else {
                        return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
                    }
                }
            } else if (key == "mac_filter") {
                if (!value.is_array() ||
                    !std::all_of(value.begin(), value.end(), [](const auto& mac) { return mac.is_string(); })) {
                    return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
                }
            } else {
                return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
            }
        }
        configuration["allow_isod2_fake_dc"] = configuration.at("allow_hlc_fake_dc");
        configuration.erase("allow_hlc_fake_dc");
        if (configuration.contains("phase_switch_configuration")) {
            configuration["phase_switch_configuration"] = {
                {"reinit_configuration", configuration.at("phase_switch_configuration")}};
        }
        return m_request_handler_ptr->set_ac_session_configuration(
            evse_index, configuration.get<types::evse_manager::ACChargingSessionConfiguration>());
    } catch (const std::exception&) {
        return {RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter};
    }
}

RPCDataTypes::ErrorResObj Evse::reinit_charging_session(int32_t evse_index,
                                                        std::optional<nlohmann::json> reinit_configuration) {
    RPCDataTypes::ErrorResObj res{};

    const auto* evse = m_dataobj.get_evse_store(evse_index);
    if (!evse) {
        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidEVSEIndex;
        return res;
    }

    types::evse_manager::ReinitConfiguration configuration;
    if (reinit_configuration.has_value()) {
        const auto& json_configuration = reinit_configuration.value();
        if (not json_configuration.is_object()) {
            res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
            return res;
        }

        for (const auto& [key, value] : json_configuration.items()) {
            if (key != "state_transition" and key != "duration") {
                res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
                return res;
            }

            try {
                if (key == "state_transition") {
                    if (not value.is_string()) {
                        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
                        return res;
                    }
                    configuration.state_transition =
                        types::evse_manager::string_to_reinit_state_enum(value.get<std::string>());
                } else {
                    if (value.is_number_unsigned()) {
                        const auto duration = value.get<uint64_t>();
                        if (duration > static_cast<uint64_t>(std::numeric_limits<int32_t>::max())) {
                            res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
                            return res;
                        }
                        configuration.duration = static_cast<int32_t>(duration);
                    } else if (value.is_number_integer()) {
                        const auto duration = value.get<int64_t>();
                        if (duration < 0 or duration > std::numeric_limits<int32_t>::max()) {
                            res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
                            return res;
                        }
                        configuration.duration = static_cast<int32_t>(duration);
                    } else {
                        res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
                        return res;
                    }
                }
            } catch (const std::exception&) {
                res.error = RPCDataTypes::ResponseErrorEnum::ErrorInvalidParameter;
                return res;
            }
        }
    }

    return m_request_handler_ptr->reinit_charging_session(evse_index, configuration);
}

} // namespace methods
