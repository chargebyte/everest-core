// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2022 Pionix GmbH and Contributors to EVerest
#include "API.hpp"
#include <external_energy_limits.hpp>
#include <utils/date.hpp>
#include <utils/yaml_loader.hpp>

namespace module {

static const auto NOTIFICATION_PERIOD = std::chrono::seconds(1);
static const std::string API_MODULE_SOURCE = "API_module";

SessionInfo::SessionInfo() :
    start_energy_import_wh(0),
    end_energy_import_wh(0),
    start_energy_export_wh(0),
    end_energy_export_wh(0),
    latest_total_w(0),
    state(State::Unknown) {
    this->start_time_point = date::utc_clock::now();
    this->end_time_point = this->start_time_point;

    uk_random_delay_remaining.countdown_s = 0;
    uk_random_delay_remaining.current_limit_after_delay_A = 0.;
    uk_random_delay_remaining.current_limit_during_delay_A = 0;
}

bool SessionInfo::is_state_charging(const SessionInfo::State current_state) {
    if (current_state == State::AuthRequired || current_state == State::Charging ||
        current_state == State::ChargingPausedEV || current_state == State::ChargingPausedEVSE) {
        return true;
    }
    return false;
}

void SessionInfo::reset() {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->state = State::Unknown;
    this->start_energy_import_wh = 0;
    this->end_energy_import_wh = 0;
    this->start_energy_export_wh = 0;
    this->end_energy_export_wh = 0;
    this->start_time_point = date::utc_clock::now();
    this->latest_total_w = 0;
    this->permanent_fault = false;
}

types::energy::ExternalLimits get_external_limits(const std::string& data, bool is_watts) {
    const auto limit = std::stof(data);
    const auto timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    types::energy::ExternalLimits external_limits;
    types::energy::ScheduleReqEntry target_entry;
    target_entry.timestamp = timestamp;

    types::energy::ScheduleReqEntry zero_entry;
    zero_entry.timestamp = timestamp;
    zero_entry.limits_to_leaves.total_power_W = {0};

    if (is_watts) {
        target_entry.limits_to_leaves.total_power_W = {std::fabs(limit), API_MODULE_SOURCE};
    } else {
        target_entry.limits_to_leaves.ac_max_current_A = {std::fabs(limit), API_MODULE_SOURCE};
    }

    if (limit > 0) {
        external_limits.schedule_import = std::vector<types::energy::ScheduleReqEntry>(1, target_entry);
    } else {
        external_limits.schedule_export = std::vector<types::energy::ScheduleReqEntry>(1, target_entry);
        external_limits.schedule_import = std::vector<types::energy::ScheduleReqEntry>(1, zero_entry);
    }
    return external_limits;
}

types::energy::ExternalLimits get_external_limits(int32_t phases, float amps) {
    const auto timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());
    types::energy::ExternalLimits external_limits;
    types::energy::ScheduleReqEntry target_entry;
    target_entry.timestamp = timestamp;

    types::energy::ScheduleReqEntry zero_entry;
    zero_entry.timestamp = timestamp;
    zero_entry.limits_to_leaves.total_power_W = {0, API_MODULE_SOURCE};

    // check if phases are 1 or 3, otherwise throw an exception
    const auto is_valid = (phases == 1 || phases == 3);
    if (is_valid) {
        target_entry.limits_to_leaves.ac_max_phase_count = {phases, API_MODULE_SOURCE};
        target_entry.limits_to_leaves.ac_min_phase_count = {phases, API_MODULE_SOURCE};
        target_entry.limits_to_leaves.ac_max_current_A = {std::fabs(amps), API_MODULE_SOURCE};
    } else {
        std::string error_msg = "Invalid phase count " + std::to_string(phases);
        throw std::out_of_range(error_msg);
    }

    if (amps > 0) {
        external_limits.schedule_import = std::vector<types::energy::ScheduleReqEntry>(1, target_entry);
    } else {
        external_limits.schedule_export = std::vector<types::energy::ScheduleReqEntry>(1, target_entry);
        external_limits.schedule_import = std::vector<types::energy::ScheduleReqEntry>(1, zero_entry);
    }
    return external_limits;
}

static void remove_error_from_list(std::vector<module::SessionInfo::Error>& list, const std::string& error_type) {
    list.erase(std::remove_if(list.begin(), list.end(),
                              [error_type](const module::SessionInfo::Error& err) { return err.type == error_type; }),
               list.end());
}

void SessionInfo::update_state(const types::evse_manager::SessionEvent event) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    using Event = types::evse_manager::SessionEventEnum;

    // using switch since some code analysis tools can detect missing cases
    // (when new events are added)
    switch (event.event) {
    case Event::Enabled:
        this->state = State::Unplugged;
        break;
    case Event::Disabled:
        this->state = State::Disabled;
        break;
    case Event::AuthRequired:
        this->state = State::AuthRequired;
        break;
    case Event::PrepareCharging:
    case Event::SessionStarted:
    case Event::TransactionStarted:
        this->state = State::Preparing;
        break;
    case Event::ChargingResumed:
    case Event::ChargingStarted:
        this->state = State::Charging;
        break;
    case Event::ChargingPausedEV:
        this->state = State::ChargingPausedEV;
        break;
    case Event::ChargingPausedEVSE:
        this->state = State::ChargingPausedEVSE;
        break;
    case Event::WaitingForEnergy:
        this->state = State::WaitingForEnergy;
        break;
    case Event::ChargingFinished:
        this->state = State::Finished;
        break;
    case Event::StoppingCharging:
        this->state = State::FinishedEV;
        break;
    case Event::TransactionFinished: {
        if (event.transaction_finished->reason == types::evse_manager::StopTransactionReason::Local) {
            this->state = State::FinishedEVSE;
        } else {
            this->state = State::Finished;
        }
        break;
    }
    case Event::PluginTimeout:
        this->state = State::AuthTimeout;
        break;
    case Event::ReservationStart:
        this->state = State::Reserved;
        break;
    case Event::ReservationEnd:
    case Event::SessionFinished:
        this->state = State::Unplugged;
        break;
    case Event::ReplugStarted:
    case Event::ReplugFinished:
    default:
        break;
    }
}

std::string SessionInfo::state_to_string(SessionInfo::State s) {
    switch (s) {
    case SessionInfo::State::Unknown:
        return "Unknown";
    case SessionInfo::State::Unplugged:
        return "Unplugged";
    case SessionInfo::State::Disabled:
        return "Disabled";
    case SessionInfo::State::Preparing:
        return "Preparing";
    case SessionInfo::State::Reserved:
        return "Reserved";
    case SessionInfo::State::AuthRequired:
        return "AuthRequired";
    case SessionInfo::State::WaitingForEnergy:
        return "WaitingForEnergy";
    case SessionInfo::State::ChargingPausedEV:
        return "ChargingPausedEV";
    case SessionInfo::State::ChargingPausedEVSE:
        return "ChargingPausedEVSE";
    case SessionInfo::State::Charging:
        return "Charging";
    case SessionInfo::State::Finished:
        return "Finished";
    case SessionInfo::State::FinishedEVSE:
        return "FinishedEVSE";
    case SessionInfo::State::FinishedEV:
        return "FinishedEV";
    case SessionInfo::State::AuthTimeout:
        return "AuthTimeout";
    }
    return "Unknown";
}

void SessionInfo::set_start_energy_import_wh(int32_t start_energy_import_wh) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->start_energy_import_wh = start_energy_import_wh;
    this->end_energy_import_wh = start_energy_import_wh;
    this->start_time_point = date::utc_clock::now();
    this->end_time_point = this->start_time_point;
}

void SessionInfo::set_end_energy_import_wh(int32_t end_energy_import_wh) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->end_energy_import_wh = end_energy_import_wh;
    this->end_time_point = date::utc_clock::now();
}

void SessionInfo::set_latest_energy_import_wh(int32_t latest_energy_wh) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    if (this->is_state_charging(this->state)) {
        this->end_time_point = date::utc_clock::now();
        this->end_energy_import_wh = latest_energy_wh;
    }
}

void SessionInfo::set_start_energy_export_wh(int32_t start_energy_export_wh) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->start_energy_export_wh = start_energy_export_wh;
    this->end_energy_export_wh = start_energy_export_wh;
    this->start_energy_export_wh_was_set = true;
}

void SessionInfo::set_end_energy_export_wh(int32_t end_energy_export_wh) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->end_energy_export_wh = end_energy_export_wh;
    this->end_energy_export_wh_was_set = true;
}

void SessionInfo::set_latest_energy_export_wh(int32_t latest_export_energy_wh) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    if (this->is_state_charging(this->state)) {
        this->end_energy_export_wh = latest_export_energy_wh;
        this->end_energy_export_wh_was_set = true;
    }
}

void SessionInfo::set_latest_total_w(double latest_total_w) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->latest_total_w = latest_total_w;
}

void SessionInfo::set_uk_random_delay_remaining(const types::uk_random_delay::CountDown& cd) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->uk_random_delay_remaining = cd;
}

void SessionInfo::set_enable_disable_source(const std::string& active_source, const std::string& active_state,
                                            const int active_priority) {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);
    this->active_enable_disable_source = active_source;
    this->active_enable_disable_state = active_state;
    this->active_enable_disable_priority = active_priority;
}

static void to_json(json& j, const SessionInfo::Error& e) {
    j = json{{"type", e.type}, {"description", e.description}, {"severity", e.severity}};
}

SessionInfo::operator std::string() {
    std::lock_guard<std::mutex> lock(this->session_info_mutex);

    auto charged_energy_wh = this->end_energy_import_wh - this->start_energy_import_wh;
    int32_t discharged_energy_wh{0};
    if ((this->start_energy_export_wh_was_set == true) && (this->end_energy_export_wh_was_set == true)) {
        discharged_energy_wh = this->end_energy_export_wh - this->start_energy_export_wh;
    }
    auto now = date::utc_clock::now();

    auto charging_duration_s =
        std::chrono::duration_cast<std::chrono::seconds>(this->end_time_point - this->start_time_point);

    json session_info = json::object({
        {"state", state_to_string(this->state)},
        {"permanent_fault", this->permanent_fault},
        {"charged_energy_wh", charged_energy_wh},
        {"discharged_energy_wh", discharged_energy_wh},
        {"latest_total_w", this->latest_total_w},
        {"charging_duration_s", charging_duration_s.count()},
        {"datetime", Everest::Date::to_rfc3339(now)},

    });

    json active_disable_enable = json::object({{"source", this->active_enable_disable_source},
                                               {"state", this->active_enable_disable_state},
                                               {"priority", this->active_enable_disable_priority}});
    session_info["active_enable_disable_source"] = active_disable_enable;

    if (uk_random_delay_remaining.countdown_s > 0) {
        json random_delay =
            json::object({{"remaining_s", uk_random_delay_remaining.countdown_s},
                          {"current_limit_after_delay_A", uk_random_delay_remaining.current_limit_after_delay_A},
                          {"current_limit_during_delay_A", uk_random_delay_remaining.current_limit_during_delay_A},
                          {"start_time", uk_random_delay_remaining.start_time.value_or("")}});
        session_info["uk_random_delay"] = random_delay;
    }

    return session_info.dump();
}

void API::init() {
    invoke_init(*p_main);

    // ensure all evse_energy_sink(s) that are connected have an evse id mapping
    for (const auto& evse_sink : this->r_evse_energy_sink) {
        if (not evse_sink->get_mapping().has_value()) {
            EVLOG_critical << "Please configure an evse mapping in your configuration file for the connected "
                              "r_evse_energy_sink with module_id: "
                           << evse_sink->module_id;
            throw std::runtime_error("At least one connected evse_energy_sink misses a mapping to an evse.");
        }
    }

    this->limit_decimal_places = std::make_unique<LimitDecimalPlaces>(this->config);
    std::vector<std::string> connectors;
    std::string var_connectors = this->api_base + "connectors";

    evse_manager_check.set_total(r_evse_manager.size());

    int evse_id = 1;
    for (auto& evse : this->r_evse_manager) {
        auto& session_info = this->info.emplace_back(std::make_unique<SessionInfo>());
        auto& hw_caps = this->hw_capabilities_str.emplace_back("");
        std::string evse_base = this->api_base + evse->module_id;
        connectors.push_back(evse->module_id);

        evse->subscribe_ready([this, &evse](bool ready) {
            if (ready) {
                this->evse_manager_check.notify_ready(evse->module_id);
            }
        });

        // API variables
        std::string var_base = evse_base + "/var/";

        std::string var_hw_caps = var_base + "hardware_capabilities";
        evse->subscribe_hw_capabilities(
            [this, var_hw_caps, &hw_caps](types::evse_board_support::HardwareCapabilities hw_capabilities) {
                hw_caps = this->limit_decimal_places->limit(hw_capabilities);
                this->mqtt.publish(var_hw_caps, hw_caps);
            });

        std::string var_powermeter = var_base + "powermeter";
        evse->subscribe_powermeter([this, var_powermeter, &session_info](types::powermeter::Powermeter powermeter) {
            this->mqtt.publish(var_powermeter, this->limit_decimal_places->limit(powermeter));
            session_info->set_latest_energy_import_wh(powermeter.energy_Wh_import.total);
            if (powermeter.energy_Wh_export.has_value()) {
                session_info->set_latest_energy_export_wh(powermeter.energy_Wh_export.value().total);
            }
            if (powermeter.power_W.has_value()) {
                session_info->set_latest_total_w(powermeter.power_W.value().total);
            }
        });

        std::string var_limits = var_base + "limits";
        evse->subscribe_limits([this, var_limits](types::evse_manager::Limits limits) {
            this->mqtt.publish(var_limits, this->limit_decimal_places->limit(limits));
        });

        std::string var_telemetry = var_base + "telemetry";
        evse->subscribe_telemetry([this, var_telemetry](types::evse_board_support::Telemetry telemetry) {
            this->mqtt.publish(var_telemetry, this->limit_decimal_places->limit(telemetry));
        });

        std::string var_ev_info = var_base + "ev_info";
        evse->subscribe_ev_info([this, var_ev_info](types::evse_manager::EVInfo ev_info) {
            json ev_info_json = ev_info;
            this->mqtt.publish(var_ev_info, ev_info_json.dump());
        });

        std::string var_selected_protocol = var_base + "selected_protocol";
        evse->subscribe_selected_protocol([this, var_selected_protocol](const std::string& selected_protocol) {
            this->selected_protocol = selected_protocol;
        });

        evse->subscribe_error(
            "evse_manager/Inoperative",
            [this, &session_info](const Everest::error::Error& error) { session_info->set_permanent_fault(true); },
            [this, &session_info](const Everest::error::Error& error) { session_info->set_permanent_fault(false); });

        std::string var_datetime = var_base + "datetime";
        std::string var_session_info = var_base + "session_info";
        std::string var_logging_path = var_base + "logging_path";
        this->api_threads.push_back(std::thread(
            [this, var_datetime, var_session_info, var_hw_caps, var_selected_protocol, &session_info, &hw_caps]() {
                auto next_tick = std::chrono::steady_clock::now();
                while (this->running) {
                    std::string datetime_str = Everest::Date::to_rfc3339(date::utc_clock::now());
                    this->mqtt.publish(var_datetime, datetime_str);
                    this->mqtt.publish(var_session_info, *session_info);
                    this->mqtt.publish(var_hw_caps, hw_caps);
                    this->mqtt.publish(var_selected_protocol, this->selected_protocol);

                    next_tick += NOTIFICATION_PERIOD;
                    std::this_thread::sleep_until(next_tick);
                }
            }));

        evse->subscribe_session_event(
            [this, var_session_info, var_logging_path, &session_info](types::evse_manager::SessionEvent session_event) {
                session_info->update_state(session_event);

                if (session_event.source.has_value()) {
                    const auto source = session_event.source.value();
                    session_info->set_enable_disable_source(
                        types::evse_manager::enable_source_to_string(source.enable_source),
                        types::evse_manager::enable_state_to_string(source.enable_state), source.enable_priority);
                }

                if (session_event.event == types::evse_manager::SessionEventEnum::SessionStarted) {
                    if (session_event.session_started.has_value()) {
                        auto session_started = session_event.session_started.value();
                        if (session_started.logging_path.has_value()) {
                            this->mqtt.publish(var_logging_path, session_started.logging_path.value());
                        }
                    }
                }
                if (session_event.event == types::evse_manager::SessionEventEnum::TransactionStarted) {
                    if (session_event.transaction_started.has_value()) {
                        auto transaction_started = session_event.transaction_started.value();
                        auto energy_Wh_import = transaction_started.meter_value.energy_Wh_import.total;
                        session_info->set_start_energy_import_wh(energy_Wh_import);

                        if (transaction_started.meter_value.energy_Wh_export.has_value()) {
                            auto energy_Wh_export = transaction_started.meter_value.energy_Wh_export.value().total;
                            session_info->set_start_energy_export_wh(energy_Wh_export);
                        } else {
                            session_info->start_energy_export_wh_was_set = false;
                        }
                    }
                } else if (session_event.event == types::evse_manager::SessionEventEnum::TransactionFinished) {
                    if (session_event.transaction_finished.has_value()) {
                        auto transaction_finished = session_event.transaction_finished.value();
                        auto energy_Wh_import = transaction_finished.meter_value.energy_Wh_import.total;
                        session_info->set_end_energy_import_wh(energy_Wh_import);

                        if (transaction_finished.meter_value.energy_Wh_export.has_value()) {
                            auto energy_Wh_export = transaction_finished.meter_value.energy_Wh_export.value().total;
                            session_info->set_end_energy_export_wh(energy_Wh_export);
                        } else {
                            session_info->end_energy_export_wh_was_set = false;
                        }
                    }

                    this->mqtt.publish(var_session_info, *session_info);
                }
            });

        // API commands
        std::string cmd_base = evse_base + "/cmd/";

        std::string cmd_enable_disable = cmd_base + "enable_disable";
        this->mqtt.subscribe(cmd_enable_disable, [this, &evse](const std::string& data) {
            auto connector_id = 0;
            types::evse_manager::EnableDisableSource enable_source{types::evse_manager::Enable_source::LocalAPI,
                                                                   types::evse_manager::Enable_state::Enable, 100};

            if (!data.empty()) {
                try {
                    auto arg = json::parse(data);
                    if (arg.contains("connector_id")) {
                        connector_id = arg.at("connector_id");
                    }
                    if (arg.contains("source")) {
                        enable_source.enable_source = types::evse_manager::string_to_enable_source(arg.at("source"));
                    }
                    if (arg.contains("state")) {
                        enable_source.enable_state = types::evse_manager::string_to_enable_state(arg.at("state"));
                    }
                    if (arg.contains("priority")) {
                        enable_source.enable_priority = arg.at("priority");
                    }

                } catch (const std::exception& e) {
                    EVLOG_error << "enable: Cannot parse argument, command ignored: " << e.what();
                    return;
                }
            } else {
                EVLOG_error << "enable: No argument specified, ignoring command";
                return;
            }
            this->evse_manager_check.wait_ready();
            evse->call_enable_disable(connector_id, enable_source);
        });

        std::string cmd_disable = cmd_base + "disable";
        this->mqtt.subscribe(cmd_disable, [this, &evse](const std::string& data) {
            auto connector_id = 0;
            types::evse_manager::EnableDisableSource enable_source{types::evse_manager::Enable_source::LocalAPI,
                                                                   types::evse_manager::Enable_state::Disable, 100};

            if (!data.empty()) {
                try {
                    connector_id = std::stoi(data);
                    EVLOG_warning << "disable: Argument is an integer, using deprecated compatibility mode";
                } catch (const std::exception& e) {
                    EVLOG_error << "disable: Cannot parse argument, ignoring command";
                    return;
                }
            } else {
                EVLOG_error << "disable: No argument specified, ignoring command";
                return;
            }
            this->evse_manager_check.wait_ready();
            evse->call_enable_disable(connector_id, enable_source);
        });

        std::string cmd_enable = cmd_base + "enable";
        this->mqtt.subscribe(cmd_enable, [this, &evse](const std::string& data) {
            auto connector_id = 0;
            types::evse_manager::EnableDisableSource enable_source{types::evse_manager::Enable_source::LocalAPI,
                                                                   types::evse_manager::Enable_state::Enable, 100};

            if (!data.empty()) {
                try {
                    connector_id = std::stoi(data);
                    EVLOG_warning << "disable: Argument is an integer, using deprecated compatibility mode";
                } catch (const std::exception& e) {
                    EVLOG_error << "disable: Cannot parse argument, ignoring command";
                    return;
                }
            } else {
                EVLOG_error << "disable: No argument specified, ignoring command";
                return;
            }
            this->evse_manager_check.wait_ready();
            evse->call_enable_disable(connector_id, enable_source);
        });

        std::string cmd_pause_charging = cmd_base + "pause_charging";
        this->mqtt.subscribe(cmd_pause_charging, [this, &evse](const std::string& data) {
            this->evse_manager_check.wait_ready();
            evse->call_pause_charging(); //
        });

        std::string cmd_resume_charging = cmd_base + "resume_charging";
        this->mqtt.subscribe(cmd_resume_charging, [this, &evse](const std::string& data) {
            this->evse_manager_check.wait_ready();
            evse->call_resume_charging(); //
        });

        std::string cmd_stop_charging = cmd_base + "stop_charging";
        this->mqtt.subscribe(cmd_stop_charging, [this, &evse](const std::string& data) {
            this->evse_manager_check.wait_ready();

            types::evse_manager::StopTransactionRequest request;
            request.reason = types::evse_manager::StopTransactionReason::Local;

            evse->call_stop_transaction(request);
        });

        std::string cmd_set_limit = cmd_base + "set_limit_amps";

        if (external_energy_limits::is_evse_sink_configured(this->r_evse_energy_sink, evse_id)) {
            auto& evse_energy_sink =
                external_energy_limits::get_evse_sink_by_evse_id(this->r_evse_energy_sink, evse_id);

            this->mqtt.subscribe(cmd_set_limit, [&evse_manager_check = this->evse_manager_check,
                                                 &evse_energy_sink = evse_energy_sink](const std::string& data) {
                try {
                    const auto external_limits = get_external_limits(data, false);
                    evse_manager_check.wait_ready();
                    evse_energy_sink.call_set_external_limits(external_limits);
                } catch (const std::invalid_argument& e) {
                    EVLOG_warning << "Invalid limit: No conversion of given input could be performed.";
                }
            });

            std::string cmd_set_limit_watts = cmd_base + "set_limit_watts";

            this->mqtt.subscribe(cmd_set_limit_watts, [&evse_manager_check = this->evse_manager_check,
                                                       &evse_energy_sink = evse_energy_sink](const std::string& data) {
                try {
                    const auto external_limits = get_external_limits(data, true);
                    evse_manager_check.wait_ready();
                    evse_energy_sink.call_set_external_limits(external_limits);
                } catch (const std::invalid_argument& e) {
                    EVLOG_warning << "Invalid limit: No conversion of given input could be performed.";
                }
            });

            std::string cmd_set_limit_phases = cmd_base + "set_limit_amps_phases";

            this->mqtt.subscribe(cmd_set_limit_phases, [&evse_manager_check = this->evse_manager_check,
                                                        &evse_energy_sink = evse_energy_sink](const std::string& data) {
                int32_t phases;
                float amps;
                try {
                    auto arg = json::parse(data);
                    if (arg.contains("amps") && arg.contains("phases")) {
                        amps = arg.at("amps");
                        phases = arg.at("phases");
                    } else {
                        EVLOG_error << "Invalid limit: Missing amps or phases.";
                        return;
                    }
                } catch (const std::exception& e) {
                    EVLOG_error << "set_limit_amps_phases: Cannot parse argument, command ignored: " << e.what();
                    return;
                }
                try {
                    const auto external_limits = get_external_limits(phases, amps);
                    evse_manager_check.wait_ready();
                    evse_energy_sink.call_set_external_limits(external_limits);
                } catch (const std::invalid_argument& e) {
                    EVLOG_warning << "Invalid limit: No conversion of given input could be performed.";
                } catch (const std::out_of_range& e) {
                    EVLOG_warning << "Invalid limit: Out of range "
                                  << ", error: " << e.what();
                }
            });
        } else {
            EVLOG_warning << "No evse energy sink configured for evse_id: " << evse_id
                          << ". API module does therefore not allow control of amps or power limits for this EVSE";
        }

        std::string cmd_force_unlock = cmd_base + "force_unlock";
        this->mqtt.subscribe(cmd_force_unlock, [this, &evse](const std::string& data) {
            int connector_id = 1;
            if (!data.empty()) {
                try {
                    connector_id = std::stoi(data);
                } catch (const std::exception& e) {
                    EVLOG_error << "Could not parse connector id for force unlock, using " << connector_id
                                << ", error: " << e.what();
                }
            }
            // match processing in ChargePointImpl::handleUnlockConnectorRequest
            // so that OCPP UnlockConnector and everest_api/evse_manager/cmd/force_unlock
            // perform the same action
            types::evse_manager::StopTransactionRequest req;
            req.reason = types::evse_manager::StopTransactionReason::UnlockCommand;
            this->evse_manager_check.wait_ready();
            evse->call_stop_transaction(req);
            evse->call_force_unlock(connector_id);
        });

        // Check if a uk_random_delay is connected that matches this evse_manager
        for (auto& random_delay : this->r_random_delay) {
            if (random_delay->module_id == evse->module_id) {

                random_delay->subscribe_countdown([&session_info](const types::uk_random_delay::CountDown& s) {
                    session_info->set_uk_random_delay_remaining(s);
                });

                std::string cmd_uk_random_delay = cmd_base + "uk_random_delay";
                this->mqtt.subscribe(cmd_uk_random_delay, [&random_delay](const std::string& data) {
                    if (data == "enable") {
                        random_delay->call_enable();
                    } else if (data == "disable") {
                        random_delay->call_disable();
                    } else if (data == "cancel") {
                        random_delay->call_cancel();
                    }
                });

                std::string uk_random_delay_set_max_duration_s = cmd_base + "uk_random_delay_set_max_duration_s";
                this->mqtt.subscribe(uk_random_delay_set_max_duration_s, [&random_delay](const std::string& data) {
                    int seconds = 600;
                    try {
                        seconds = std::stoi(data);
                    } catch (const std::exception& e) {
                        EVLOG_error << "Could not parse connector duration value for "
                                       "uk_random_delay_set_max_duration_s, using default value of "
                                    << seconds << " seconds, error: " << e.what();
                    }
                    random_delay->call_set_duration_s(seconds);
                });
            }
        }
        evse_id++;
    }

    std::string var_ocpp_connection_status = this->api_base + "ocpp/var/connection_status";
    std::string var_ocpp_schedule = this->api_base + "ocpp/var/charging_schedules";

    if (this->r_ocpp.size() == 1) {

        this->r_ocpp.at(0)->subscribe_is_connected([this](bool is_connected) {
            std::scoped_lock lock(ocpp_data_mutex);
            if (is_connected) {
                this->ocpp_connection_status = "connected";
            } else {
                this->ocpp_connection_status = "disconnected";
            }
        });

        this->r_ocpp.at(0)->subscribe_charging_schedules([this, &var_ocpp_schedule](json schedule) {
            std::scoped_lock lock(ocpp_data_mutex);
            this->ocpp_charging_schedule = schedule;
            this->ocpp_charging_schedule_updated = true;
        });
    }

    std::string var_info = this->api_base + "info/var/info";

    if (this->config.charger_information_file != "") {
        auto charger_information_path = std::filesystem::path(this->config.charger_information_file);
        try {
            this->charger_information = Everest::load_yaml(charger_information_path);

        } catch (const std::exception& err) {
            EVLOG_error << "Error parsing charger information file at " << this->config.charger_information_file << ": "
                        << err.what();
        }
    }

    this->api_threads.push_back(
        std::thread([this, var_connectors, connectors, var_info, var_ocpp_connection_status, var_ocpp_schedule]() {
            auto next_tick = std::chrono::steady_clock::now();
            while (this->running) {
                json connectors_array = connectors;
                this->mqtt.publish(var_connectors, connectors_array.dump());
                if (not this->charger_information.is_null()) {
                    this->mqtt.publish(var_info, this->charger_information.dump());
                }
                {
                    std::scoped_lock lock(ocpp_data_mutex);
                    this->mqtt.publish(var_ocpp_connection_status, this->ocpp_connection_status);
                    if (this->ocpp_charging_schedule_updated) {
                        this->ocpp_charging_schedule_updated = false;
                        this->mqtt.publish(var_ocpp_schedule, ocpp_charging_schedule.dump());
                    }
                }

                next_tick += NOTIFICATION_PERIOD;
                std::this_thread::sleep_until(next_tick);
            }
        }));
}

void API::ready() {
    invoke_ready(*p_main);

    std::string var_active_errors = this->api_base + "errors/var/active_errors";
    this->api_threads.push_back(std::thread([this, var_active_errors]() {
        auto next_tick = std::chrono::steady_clock::now();
        while (this->running) {
            std::string datetime_str = Everest::Date::to_rfc3339(date::utc_clock::now());

            if (not r_error_history.empty()) {
                // request active errors
                types::error_history::FilterArguments filter;
                filter.state_filter = types::error_history::State::Active;
                auto active_errors = r_error_history.at(0)->call_get_errors(filter);
                json errors_json = json(active_errors);

                // publish
                this->mqtt.publish(var_active_errors, errors_json.dump());
            }
            next_tick += NOTIFICATION_PERIOD;
            std::this_thread::sleep_until(next_tick);
        }
    }));
}

} // namespace module
