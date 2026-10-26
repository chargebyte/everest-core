// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "meter.hpp"
#include <everest/logging.hpp>
#include <thread>
#include <utils/date.hpp>

namespace ast_app_layer {

namespace {

using C = CommandType;
using R = CommandResult;

std::runtime_error command_error(const Response& response) {
    return std::runtime_error("AST command " +
                              module::conversions::hexdump(static_cast<std::uint16_t>(response.command)) + ": " +
                              command_result_to_string(response.status));
}

void require_size(const Bytes& data, std::size_t size) {
    if (data.size() != size)
        throw std::runtime_error("Invalid AST response payload size");
}

std::uint32_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

} // namespace

Meter::Meter(std::unique_ptr<Transport> transport, std::string variant, std::int8_t offset) :
    transport(std::move(transport)), configured_variant(std::move(variant)), gmt_offset(offset) {
}

std::vector<Response> Meter::exchange(const std::vector<Bytes>& requests) {
    return transport->exchange(requests);
}

Response Meter::request(C type, const Bytes& data) {
    const auto responses = exchange({command(type, data)});
    if (responses.size() != 1 || responses.front().command != type) {
        throw std::runtime_error("Missing or unexpected AST response");
    }
    return responses.front();
}

Bytes Meter::read(C type, const Bytes& data) {
    auto response = request(type, data);
    if (response.status != R::OK)
        throw command_error(response);
    return response.data;
}

std::optional<Bytes> Meter::read_optional(C type) {
    if (unsupported.count(type))
        return std::nullopt;
    const auto response = request(type);
    if (response.status == R::LOGICAL_ADDRESS_NOT_FOUND) {
        unsupported.insert(type);
        if (type == C::TEMPERATURE) {
            EVLOG_info << "AST internal temperature is not supported; disabling its acquisition";
        } else {
            EVLOG_info << "AST optional command " << module::conversions::hexdump(static_cast<std::uint16_t>(type))
                       << " is not supported; disabling it";
        }
        return std::nullopt;
    }
    if (response.status != R::OK) {
        if (!command_errors.count(type) || command_errors[type] != response.status) {
            EVLOG_error << command_error(response).what();
            command_errors[type] = response.status;
        }
        return std::nullopt;
    }
    if (command_errors.erase(type))
        EVLOG_info << "AST optional command " << module::conversions::hexdump(static_cast<std::uint16_t>(type))
                   << " recovered";
    return response.data;
}

MeterInfo Meter::initialize() {
    std::scoped_lock lock(mutex);
    const auto charge_id = read(C::CHARGE_POINT_ID);
    if (configured_variant == "auto")
        meter_info.variant = detect_variant(charge_id);
    else if (configured_variant == "dc_meter_v4_9")
        meter_info.variant = ProtocolVariant::dc_meter_v4_9;
    else if (configured_variant == "detached_display_v6_5")
        meter_info.variant = ProtocolVariant::detached_display_v6_5;
    else
        throw std::invalid_argument("Unknown AST protocol_variant");
    const auto expected = meter_info.variant == ProtocolVariant::dc_meter_v4_9 ? 14u : 38u;
    require_size(charge_id, expected);
    device_diagnostics.charge_point_id_type = charge_id.front();
    meter_info.charge_point_id = string(Bytes(charge_id.begin() + 1, charge_id.end()));
    device_diagnostics.charge_point_id = meter_info.charge_point_id;
    meter_info.meter_id = string(read(C::AB_SERVER_ID));
    device_diagnostics.app_board.server_id = meter_info.meter_id;
    device_diagnostics.app_board.type = string(read(C::AB_DEVICE_TYPE));
    EVLOG_info << "AST " << device_diagnostics.app_board.type << ", meter " << meter_info.meter_id << ", protocol "
               << (meter_info.variant == ProtocolVariant::dc_meter_v4_9 ? "DC-Meter 4.9" : "Detached Display 6.5")
               << ", charge point " << meter_info.charge_point_id;
    // The device may still have a transaction from before this process started.
    state = State::unknown;
    try {
        read_public_key();
    } catch (const std::exception& error) {
        EVLOG_warning << "AST OCMF key unavailable: " << error.what();
    }
    Bytes time;
    const auto now = unix_now();
    for (unsigned i = 0; i < 4; ++i)
        time.push_back((now >> (8 * i)) & 0xff);
    time.push_back(static_cast<std::uint8_t>(gmt_offset));
    require_size(read(C::TIME, time), 0);
    return meter_info;
}

void Meter::read_public_key() {
    if (const auto data = read_optional(C::GET_PUBKEY_STR16)) {
        meter_info.public_key = public_key(*data);
        device_diagnostics.pubkey_str16 = meter_info.public_key;
        device_diagnostics.pubkey_str16_format = 4;
    }
}

MeterInfo Meter::info() {
    std::scoped_lock lock(mutex);
    return meter_info;
}

types::powermeter::Powermeter Meter::sample() {
    std::scoped_lock lock(mutex);
    const std::vector<C> types{C::GET_DEV_VOLTAGE_L1,          C::GET_CURRENT_L1,
                               C::GET_IMPORT_DEV_POWER,        C::GET_EXPORT_DEV_POWER,
                               C::GET_TOTAL_IMPORT_DEV_ENERGY, C::GET_TOTAL_EXPORT_DEV_ENERGY};
    std::vector<Bytes> requests;
    for (auto type : types)
        requests.push_back(command(type));
    const auto responses = exchange(requests);
    std::map<C, Bytes> values;
    for (const auto& response : responses) {
        if (response.status != R::OK)
            throw command_error(response);
        values.emplace(response.command, response.data);
    }
    for (auto type : types)
        require_size(values.at(type),
                     type == C::GET_TOTAL_IMPORT_DEV_ENERGY || type == C::GET_TOTAL_EXPORT_DEV_ENERGY ? 8 : 4);
    types::powermeter::Powermeter result;
    result.meter_id = meter_info.meter_id;
    result.energy_Wh_import.total = unsigned_le(values.at(C::GET_TOTAL_IMPORT_DEV_ENERGY), 0, 8) / 10.0;
    types::units::Energy exported;
    exported.total = unsigned_le(values.at(C::GET_TOTAL_EXPORT_DEV_ENERGY), 0, 8) / 10.0;
    result.energy_Wh_export = exported;
    types::units::Voltage voltage;
    voltage.DC = unsigned_le(values.at(C::GET_DEV_VOLTAGE_L1), 0, 4) / 100.0;
    result.voltage_V = voltage;
    types::units::Current current;
    current.DC = signed_le(values.at(C::GET_CURRENT_L1), 0, 4) / 1000.0;
    result.current_A = current;
    types::units::Power power;
    power.total = unsigned_le(values.at(C::GET_IMPORT_DEV_POWER), 0, 4) / 100.0 -
                  unsigned_le(values.at(C::GET_EXPORT_DEV_POWER), 0, 4) / 100.0;
    result.power_W = power;
    device_data.total_dev_import_energy_Wh = unsigned_le(values.at(C::GET_TOTAL_IMPORT_DEV_ENERGY), 0, 8) / 10.0;
    device_data.total_dev_export_energy_Wh = unsigned_le(values.at(C::GET_TOTAL_EXPORT_DEV_ENERGY), 0, 8) / 10.0;
    try {
        if (const auto data = read_optional(C::TEMPERATURE)) {
            require_size(*data, 2);
            types::temperature::Temperature temperature;
            temperature.temperature = signed_le(*data, 0, 2) / 10.0;
            temperature.identification = "meter_internal";
            temperature.location = "DC meter";
            result.temperatures = std::vector<types::temperature::Temperature>{temperature};
            if (!temperature_seen) {
                temperature_seen = true;
                EVLOG_info << "AST internal temperature available on powermeter and temperature_sensor interfaces";
            }
        }
    } catch (const std::exception& error) {
        if (optional_temperature_error != error.what()) {
            EVLOG_error << "AST temperature acquisition failed: " << error.what();
            optional_temperature_error = error.what();
        }
    }
    if (result.temperatures && !optional_temperature_error.empty()) {
        EVLOG_info << "AST temperature acquisition restored";
        optional_temperature_error.clear();
    }
    result.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    return result;
}

void Meter::start(const types::powermeter::TransactionReq& request_value) {
    std::scoped_lock lock(mutex);
    if (state == State::running && active_id == request_value.transaction_id)
        return;
    if (state != State::idle)
        throw std::runtime_error(
            "AST transaction is active or uncertain; call stop_transaction with empty ID to clear it first");
    const auto start = start_command(request_value, meter_info.variant, gmt_offset, unix_now());
    if (meter_info.public_key.empty())
        read_public_key();
    if (meter_info.public_key.empty())
        throw std::runtime_error("AST OCMF public key is unavailable");
    if (request_value.evse_id != meter_info.charge_point_id) {
        EVLOG_warning << "AST uses stored charge point " << meter_info.charge_point_id << "; requested EVSE ID differs";
    }
    if (request_value.identification_level.has_value() || !request_value.identification_flags.empty()) {
        EVLOG_debug << "AST start format has no identification level/flags fields";
    }
    if (meter_info.variant == ProtocolVariant::dc_meter_v4_9) {
        std::this_thread::sleep_until(last_start + std::chrono::seconds(2));
    }
    last_start = std::chrono::steady_clock::now();
    state = State::unknown;
    active_id = request_value.transaction_id;
    completed.reset();
    const auto response = request(C::START_TRANSACTION, Bytes(start.begin() + 5, start.end()));
    if (response.status != R::OK) {
        state = State::idle;
        active_id.clear();
        throw command_error(response);
    }
    require_size(response.data, 0);
    state = State::running;
    EVLOG_info << "AST transaction " << active_id << " started";
}

types::units_signed::SignedMeterValue Meter::read_ocmf(const std::string& id) {
    if (meter_info.public_key.empty())
        read_public_key();
    if (meter_info.public_key.empty())
        throw std::runtime_error("AST OCMF public key is unavailable");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (true) {
        bool associated = meter_info.variant == ProtocolVariant::detached_display_v6_5 && find_ocmf_supported;
        Bytes id_bytes(id.begin(), id.end());
        id_bytes.push_back(0);
        auto response =
            request(associated ? C::FIND_TRANSACTION_OCMF : C::GET_LAST_OCMF, associated ? id_bytes : Bytes{});
        if (associated && response.status == R::LOGICAL_ADDRESS_NOT_FOUND) {
            find_ocmf_supported = false;
            EVLOG_info << "AST transaction-specific OCMF lookup unavailable; using last OCMF with ID validation";
            response = request(C::GET_LAST_OCMF);
            associated = false;
        }
        if (response.status == R::OK) {
            const auto report = validate_ocmf(response.data, id, associated);
            types::units_signed::SignedMeterValue value;
            value.signed_meter_data = report.text;
            value.signing_method = report.signing_method;
            value.encoding_method = "OCMF";
            value.public_key = meter_info.public_key;
            device_data.last_ocmf_transaction = report.text;
            return value;
        }
        if (response.status != R::BUSY && response.status != R::NOT_AVAILABLE && response.status != R::OUT_OF_RANGE) {
            throw command_error(response);
        }
        if (std::chrono::steady_clock::now() >= deadline)
            throw command_error(response);
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
}

std::optional<types::units_signed::SignedMeterValue> Meter::stop(const std::string& id) {
    std::scoped_lock lock(mutex);
    if (!id.empty() && completed && active_id == id)
        return completed;
    if (!id.empty() && (state == State::unknown || (!active_id.empty() && active_id != id))) {
        throw std::runtime_error("AST transaction ID does not match active transaction or state is uncertain");
    }
    if (id.empty() || state == State::running) {
        state = State::unknown;
        const auto response = request(C::STOP_TRANSACTION);
        bool inactive = false;
        if (response.status == R::NOT_AVAILABLE || response.status == R::REJECTED) {
            // Status alone is ambiguous. Confirm the documented 'not in transaction' error.
            const auto errors = read(C::GET_ERRORS, {0, 0});
            require_size(errors, 50);
            inactive = unsigned_le(errors, 0, 4) == 0x06011627;
        }
        if (response.status != R::OK && !inactive)
            throw command_error(response);
        if (response.status == R::OK)
            require_size(response.data, 0);
        state = id.empty() ? State::idle : State::stopped;
        if (id.empty()) {
            active_id.clear();
            completed.reset();
            EVLOG_info << "AST transaction state cleared";
            return std::nullopt;
        }
    }
    completed = read_ocmf(id);
    active_id = id;
    state = State::idle;
    EVLOG_info << "AST transaction " << id << " completed; signed OCMF read";
    return completed;
}

DiagnosticSnapshot Meter::diagnostics(bool include_data, bool include_diagnostics) {
    std::scoped_lock lock(mutex);
    if (include_data) {
        const std::vector<std::pair<C, std::string>> energies{
            {C::GET_TOTAL_IMPORT_MAINS_ENERGY, "total_mains_import_energy_Wh"},
            {C::GET_TOTAL_EXPORT_MAINS_ENERGY, "total_mains_export_energy_Wh"},
            {C::GET_TOTAL_IMPORT_LINE_LOSS_ENERGY, "total_import_line_loss_energy_Wh"},
            {C::GET_TOTAL_EXPORT_LINE_LOSS_ENERGY, "total_export_line_loss_energy_Wh"},
            {C::GET_TRANSACT_IMPORT_LINE_LOSS_ENERGY, "transaction_import_line_loss_energy_Wh"},
            {C::GET_TRANSACT_EXPORT_LINE_LOSS_ENERGY, "transaction_export_line_loss_energy_Wh"},
            {C::GET_TRANSACT_TOTAL_IMPORT_DEV_ENERGY, "transaction_import_device_energy_Wh"},
            {C::GET_TRANSACT_TOTAL_EXPORT_DEV_ENERGY, "transaction_export_device_energy_Wh"},
            {C::GET_TRANSACT_TOTAL_IMPORT_MAINS_ENERGY, "transaction_import_mains_energy_Wh"},
            {C::GET_TRANSACT_TOTAL_EXPORT_MAINS_ENERGY, "transaction_export_mains_energy_Wh"},
            {C::GET_TOTAL_START_IMPORT_DEV_ENERGY, "total_start_import_energy_Wh"},
            {C::GET_TOTAL_STOP_IMPORT_DEV_ENERGY, "total_stop_import_energy_Wh"},
            {C::GET_TOTAL_START_EXPORT_DEV_ENERGY, "total_start_export_energy_Wh"},
            {C::GET_TOTAL_STOP_EXPORT_DEV_ENERGY, "total_stop_export_energy_Wh"},
            {C::GET_TOTAL_START_IMPORT_MAINS_ENERGY, "total_start_import_mains_energy_Wh"},
            {C::GET_TOTAL_STOP_IMPORT_MAINS_ENERGY, "total_stop_import_mains_energy_Wh"},
            {C::GET_TOTAL_START_EXPORT_MAINS_ENERGY, "total_start_export_mains_energy_Wh"},
            {C::GET_TOTAL_STOP_EXPORT_MAINS_ENERGY, "total_stop_export_mains_energy_Wh"},
            {C::GET_TOTAL_START_IMPORT_LINE_LOSS_ENERGY, "total_start_import_line_loss_energy_Wh"},
            {C::GET_TOTAL_STOP_IMPORT_LINE_LOSS_ENERGY, "total_stop_import_line_loss_energy_Wh"},
            {C::GET_TOTAL_START_EXPORT_LINE_LOSS_ENERGY, "total_start_export_line_loss_energy_Wh"},
            {C::GET_TOTAL_STOP_EXPORT_LINE_LOSS_ENERGY, "total_stop_export_line_loss_energy_Wh"}};
        for (const auto& [type, name] : energies) {
            additional_values.erase(name);
            if (const auto value = read_optional(type)) {
                require_size(*value, 8);
                additional_values[name] = unsigned_le(*value, 0, 8) / 10.0;
            }
        }
        const std::vector<std::pair<C, std::string>> instantaneous{
            {C::GET_VOLTAGE_L1, "mains_voltage_V"},
            {C::GET_IMPORT_DEV_POWER, "import_device_power_W"},
            {C::GET_EXPORT_DEV_POWER, "export_device_power_W"},
            {C::GET_TOTAL_IMPORT_MAINS_POWER, "import_mains_power_W"},
            {C::GET_TOTAL_EXPORT_MAINS_POWER, "export_mains_power_W"},
            {C::GET_IMPORT_LINE_LOSS_POWER, "import_line_loss_power_W"},
            {C::GET_EXPORT_LINE_LOSS_POWER, "export_line_loss_power_W"}};
        for (const auto& [type, name] : instantaneous) {
            additional_values.erase(name);
            if (const auto value = read_optional(type)) {
                require_size(*value, 4);
                additional_values[name] = unsigned_le(*value, 0, 4) / 100.0;
            }
        }
        additional_values.erase("total_device_power_W");
        if (const auto value = read_optional(C::GET_TOTAL_DEV_POWER)) {
            require_size(*value, 8);
            additional_values["total_device_power_W"] = signed_le(*value, 0, 8) / 100.0;
        }
        if (const auto value = read_optional(C::TIME)) {
            require_size(*value, 5);
            device_data.utc_time_s = unsigned_le(*value, 0, 4);
            device_data.gmt_offset_quarterhours = signed_le(*value, 4, 1);
        }
        if (const auto value = read_optional(C::AB_STATUS)) {
            require_size(*value, 8);
            device_data.ab_status = unsigned_le(*value, 0, 8);
        }
        if (const auto value = read_optional(C::GET_TRANSACT_TOTAL_DURATION)) {
            require_size(*value, 4);
            device_data.total_transaction_duration_s = unsigned_le(*value, 0, 4);
        }
        if (const auto value = read_optional(C::OCMF_STATS)) {
            require_size(*value, 16);
            device_data.ocmf_stats = {static_cast<std::uint32_t>(unsigned_le(*value, 0, 4)),
                                      static_cast<std::uint32_t>(unsigned_le(*value, 4, 4)),
                                      static_cast<std::uint32_t>(unsigned_le(*value, 8, 4)),
                                      static_cast<std::uint32_t>(unsigned_le(*value, 12, 4))};
        }
        if (const auto value = read_optional(C::GET_LAST_OCMF)) {
            device_data.last_ocmf_transaction = string(*value);
        }
        if (const auto value = read_optional(C::OCMF_INFO)) {
            std::size_t offset = 0;
            const auto field = [&](std::size_t maximum) {
                if (offset >= value->size())
                    throw std::runtime_error("Truncated OCMF metadata");
                const auto length = value->at(offset++);
                if (length > maximum || length > value->size() - offset)
                    throw std::runtime_error("Invalid OCMF metadata length");
                const auto text = string(Bytes(value->begin() + offset, value->begin() + offset + length));
                offset += length;
                return text;
            };
            device_data.ocmf_info.gateway_id = field(18);
            device_data.ocmf_info.manufacturer = field(4);
            device_data.ocmf_info.model = field(18);
        }
    }
    if (include_diagnostics) {
        if (const auto value = read_optional(C::AB_SW_VERSION))
            device_diagnostics.app_board.sw_ver = string(*value);
        if (const auto value = read_optional(C::MB_SW_VERSION))
            device_diagnostics.m_board.sw_ver = string(*value);
        if (const auto value = read_optional(C::AB_HW_VERSION)) {
            const auto text = string(*value);
            const auto separator = text.find('|');
            device_diagnostics.app_board.hw_ver = text.substr(0, separator);
            device_diagnostics.m_board.hw_ver = separator == std::string::npos ? "" : text.substr(separator + 1);
        }
        if (const auto value = read_optional(C::AB_SERIAL_NR)) {
            require_size(*value, 4);
            device_diagnostics.app_board.serial_number = unsigned_le(*value, 0, 4);
        }
        if (const auto value = read_optional(C::AB_MODE_SET)) {
            require_size(*value, 1);
            device_diagnostics.app_board.mode = value->front();
        }
        if (const auto value = read_optional(C::AB_FW_CHECKSUM))
            device_diagnostics.app_board.fw_crc = unsigned_le(*value, 0, 2);
        if (const auto value = read_optional(C::AB_FW_HASH))
            device_diagnostics.app_board.fw_hash = unsigned_le(*value, 0, 2);
        if (const auto value = read_optional(C::MB_FW_CHECKSUM))
            device_diagnostics.m_board.fw_crc = unsigned_le(*value, 0, 2);
        if (const auto value = read_optional(C::OCMF_CONFIG)) {
            require_size(*value, 48);
            device_diagnostics.ocmf_config_table = *value;
        }
        if (const auto value = read_optional(C::GET_PUBKEY_ASN1))
            device_diagnostics.pubkey_asn1 = string(*value);
        if (const auto value = read_optional(C::GET_PUBKEY_BIN)) {
            require_size(*value, 65);
            device_diagnostics.pubkey_format = value->front();
            device_diagnostics.pubkey = module::conversions::hexdump(Bytes(value->begin() + 1, value->end()));
        }
        if (const auto value = read_optional(C::GET_LOG_STATS)) {
            require_size(*value, 16);
            device_diagnostics.log_stats = {static_cast<std::uint32_t>(unsigned_le(*value, 0, 4)),
                                            static_cast<std::uint32_t>(unsigned_le(*value, 4, 4)),
                                            static_cast<std::uint32_t>(unsigned_le(*value, 8, 4)),
                                            static_cast<std::uint32_t>(unsigned_le(*value, 12, 4))};
        }
        if (const auto value = read_optional(C::GET_LAST_LOG_ENTRY)) {
            require_size(*value, 104);
            auto& log = logging.last_log;
            log.type = static_cast<LogType>(value->front());
            log.second_index = unsigned_le(*value, 1, 4);
            log.utc_time = unsigned_le(*value, 5, 4);
            log.utc_offset = value->at(9);
            log.old_value = Bytes(value->begin() + 10, value->begin() + 20);
            log.new_value = Bytes(value->begin() + 20, value->begin() + 30);
            log.server_id = Bytes(value->begin() + 30, value->begin() + 40);
            log.signature = Bytes(value->begin() + 40, value->end());
        }
        for (std::uint8_t source = 0; source < 2; ++source) {
            for (std::uint8_t category = 0; category < 4; ++category) {
                const auto value = read(C::GET_ERRORS, {category, source});
                require_size(value, 50);
                for (std::size_t i = 0; i < 5; ++i) {
                    auto& error = logging.source[source].category[category].error[i];
                    error.id = unsigned_le(value, i * 10, 4);
                    error.priority = unsigned_le(value, i * 10 + 4, 2);
                    error.counter = unsigned_le(value, i * 10 + 6, 4);
                }
            }
        }
    }
    auto data = nlohmann::json(device_data);
    data.update(additional_values);
    return {data, nlohmann::json(device_diagnostics), nlohmann::json(logging)};
}

} // namespace ast_app_layer
