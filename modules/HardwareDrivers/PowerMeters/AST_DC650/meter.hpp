// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once
#include "diagnostics.hpp"
#include "transport.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <set>

namespace ast_app_layer {

struct MeterInfo {
    ProtocolVariant variant;
    std::string meter_id;
    std::string charge_point_id;
    std::string public_key;
};

struct DiagnosticSnapshot {
    nlohmann::json device_data;
    nlohmann::json device_diagnostics;
    nlohmann::json logging;
};

// The lock spans complete operations, including start/stop and OCMF readout.
class Meter {
public:
    Meter(std::unique_ptr<Transport> transport, std::string variant, std::int8_t gmt_offset);
    MeterInfo initialize();
    types::powermeter::Powermeter sample();
    void start(const types::powermeter::TransactionReq& request);
    std::optional<types::units_signed::SignedMeterValue> stop(const std::string& transaction_id);
    DiagnosticSnapshot diagnostics(bool device_data, bool device_diagnostics);
    MeterInfo info();

private:
    enum class State
    {
        unknown,
        idle,
        running,
        stopped
    };
    std::mutex mutex;
    std::unique_ptr<Transport> transport;
    std::string configured_variant;
    std::int8_t gmt_offset;
    MeterInfo meter_info{};
    State state{State::unknown};
    std::string active_id;
    std::optional<types::units_signed::SignedMeterValue> completed;
    std::set<CommandType> unsupported;
    std::map<CommandType, CommandResult> command_errors;
    std::string optional_temperature_error;
    bool find_ocmf_supported{true};
    bool temperature_seen{false};
    std::chrono::steady_clock::time_point last_start{};
    module::DeviceData device_data{};
    module::DeviceDiagnostics device_diagnostics{};
    module::Logging logging{};
    nlohmann::json additional_values = nlohmann::json::object();

    std::vector<Response> exchange(const std::vector<Bytes>& requests);
    Response request(CommandType type, const Bytes& data = {});
    Bytes read(CommandType type, const Bytes& data = {});
    std::optional<Bytes> read_optional(CommandType type);
    void read_public_key();
    types::units_signed::SignedMeterValue read_ocmf(const std::string& id);
};

} // namespace ast_app_layer
