// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef REQUESTHANDLERDUMMY_HPP
#define REQUESTHANDLERDUMMY_HPP

#include "../data/DataStore.hpp"

#include <../rpc/RequestHandlerInterface.hpp>
#include <generated/types/json_rpc_api.hpp>

class RequestHandlerDummy : public request_interface::RequestHandlerInterface {
public:
    RequestHandlerDummy() = delete;
    explicit RequestHandlerDummy(data::DataStoreCharger& data_store);
    ~RequestHandlerDummy() override = default;

    types::json_rpc_api::ErrorResObj set_charging_allowed(const int32_t evse_index, bool charging_allowed) override;
    types::json_rpc_api::ErrorResObj set_ac_charging(const int32_t evse_index, bool charging_allowed, bool max_current,
                                                     std::optional<int> phase_count) override;
    types::json_rpc_api::ErrorResObj set_ac_charging_current(const int32_t evse_index, float max_current) override;
    types::json_rpc_api::ErrorResObj set_ac_charging_phase_count(const int32_t evse_index, int phase_count) override;
    types::json_rpc_api::ErrorResObj set_dc_charging(const int32_t evse_index, bool charging_allowed,
                                                     float max_power) override;
    types::json_rpc_api::ErrorResObj set_ac_charging_session_configuration(
        const int32_t evse_index, bool allow_isod20, bool allow_isod2, bool allow_din, bool allow_hlc_fake_dc,
        std::optional<bool> disable_isod2_fake_dc_after_replug,
        std::optional<types::json_rpc_api::ReinitConfigurationObj> reinit_configuration,
        std::optional<types::json_rpc_api::ReinitStateEnum> phase_switch_state_transition,
        std::optional<int> phase_switch_duration, std::optional<std::vector<std::string>> mac_filter) override;
    types::json_rpc_api::ErrorResObj set_dc_charging_power(const int32_t evse_index, float max_power) override;
    types::json_rpc_api::ErrorResObj enable_connector(const int32_t evse_index, int connector_id, bool enable,
                                                      int priority) override;
    types::json_rpc_api::ErrorResObj
    reinit_charging_session(const int32_t evse_index,
                            std::optional<types::json_rpc_api::ReinitConfigurationObj> reinit_configuration) override;

private:
    data::DataStoreCharger& data_store;
};

#endif // REQUESTHANDLERDUMMY_HPP
