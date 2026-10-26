// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "powermeterImpl.hpp"

namespace module {
namespace main {

powermeterImpl::~powermeterImpl() {
    {
        std::scoped_lock lock(shutdown_mutex);
        shutting_down = true;
    }
    shutdown_condition.notify_all();
    if (worker.joinable())
        worker.join();
}

void powermeterImpl::init() {
    try {
        auto transport = std::make_unique<ast_app_layer::SerialTransport>(
            config.serial_port, config.baudrate, config.parity, config.powermeter_device_id, config.ignore_echo);
        meter = std::make_unique<ast_app_layer::Meter>(std::move(transport), config.protocol_variant,
                                                       config.gmt_offset_quarter_hours);
        meter->initialize();
    } catch (const std::exception& error) {
        EVLOG_AND_THROW(Everest::EverestConfigError(error.what()));
    }
}

void powermeterImpl::ready() {
    publish_key();
    worker = std::thread([this] {
        auto next_diagnostics = std::chrono::steady_clock::now();
        while (true) {
            {
                std::scoped_lock lock(shutdown_mutex);
                if (shutting_down)
                    break;
            }
            try {
                const auto values = meter->sample();
                publish_measurement(values);
                publish_key();
                if (!last_poll_error.empty()) {
                    EVLOG_info << "AST measurement acquisition restored";
                    last_poll_error.clear();
                }
            } catch (const std::exception& error) {
                if (last_poll_error != error.what()) {
                    EVLOG_error << "AST measurement acquisition failed: " << error.what();
                    last_poll_error = error.what();
                }
            }
            if ((config.publish_device_data || config.publish_device_diagnostics) &&
                std::chrono::steady_clock::now() >= next_diagnostics) {
                try {
                    const auto snapshot =
                        meter->diagnostics(config.publish_device_data, config.publish_device_diagnostics);
                    const auto topic = meter->info().meter_id;
                    if (config.publish_device_data)
                        mod->mqtt.publish(topic + "/device_data", snapshot.device_data.dump());
                    if (config.publish_device_diagnostics) {
                        mod->mqtt.publish(topic + "/device_diagnostics", snapshot.device_diagnostics.dump());
                        mod->mqtt.publish(topic + "/logging", snapshot.logging.dump());
                    }
                    if (!last_diagnostic_error.empty()) {
                        EVLOG_info << "AST diagnostics acquisition restored";
                        last_diagnostic_error.clear();
                    }
                } catch (const std::exception& error) {
                    if (last_diagnostic_error != error.what()) {
                        EVLOG_error << "AST diagnostics acquisition failed: " << error.what();
                        last_diagnostic_error = error.what();
                    }
                }
                next_diagnostics = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            }
            std::unique_lock lock(shutdown_mutex);
            if (shutdown_condition.wait_for(lock, std::chrono::seconds(1), [this] { return shutting_down; }))
                break;
        }
    });
}

types::powermeter::TransactionStartResponse
powermeterImpl::handle_start_transaction(types::powermeter::TransactionReq& value) {
    types::powermeter::TransactionStartResponse result;
    try {
        meter->start(value);
        result.status = types::powermeter::TransactionRequestStatus::OK;
    } catch (const std::exception& error) {
        result.status = types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR;
        result.error = error.what();
        EVLOG_error << "AST transaction start failed: " << error.what();
    }
    return result;
}

types::powermeter::TransactionStopResponse powermeterImpl::handle_stop_transaction(std::string& transaction_id) {
    types::powermeter::TransactionStopResponse result;
    try {
        result.signed_meter_value = meter->stop(transaction_id);
        result.status = types::powermeter::TransactionRequestStatus::OK;
    } catch (const std::exception& error) {
        result.status = types::powermeter::TransactionRequestStatus::UNEXPECTED_ERROR;
        result.error = error.what();
        EVLOG_error << "AST transaction stop failed: " << error.what();
    }
    return result;
}

void powermeterImpl::publish_key() {
    const auto key = meter->info().public_key;
    if (!key.empty() && key != last_key) {
        publish_public_key_ocmf(key);
        last_key = key;
        EVLOG_info << "AST OCMF public key published";
    }
}

void powermeterImpl::publish_measurement(const types::powermeter::Powermeter& values) {
    publish_powermeter(values);
    if (values.temperatures)
        mod->p_temperature->publish_temperatures(*values.temperatures);
}

} // namespace main
} // namespace module
