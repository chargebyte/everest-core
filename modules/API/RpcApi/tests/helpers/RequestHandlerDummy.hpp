// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef REQUESTHANDLERDUMMY_HPP
#define REQUESTHANDLERDUMMY_HPP

#include "../data/DataStore.hpp"

#include <../rpc/RequestHandlerInterface.hpp>
#include <types/json_rpc_api/json_rpc_api.hpp>

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
    types::json_rpc_api::ErrorResObj set_dc_charging_power(const int32_t evse_index, float max_power) override;
    types::json_rpc_api::ErrorResObj enable_connector(const int32_t evse_index, int connector_id, bool enable,
                                                      int priority) override;
    types::json_rpc_api::ErrorResObj
    reinit_charging_session(int32_t evse_index,
                            const types::evse_manager::ReinitConfiguration& reinit_configuration) override;

    static void set_reinit_result(bool result);
    static types::evse_manager::ReinitConfiguration last_reinit_configuration;

private:
    data::DataStoreCharger& data_store;
    static bool reinit_result;
};

#endif // REQUESTHANDLERDUMMY_HPP
