// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "fsm/misc.hpp"
#include <everest/slac/telemetry.hpp>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace everest::lib::slac {

namespace {

std::string to_string(SlacState state) {
    switch (state) {
    case SlacState::Init:
        return "Init";
    case SlacState::Reset:
        return "Reset";
    case SlacState::ResetChip:
        return "ResetChip";
    case SlacState::Idle:
        return "Idle";
    case SlacState::Failed:
        return "Failed";
    case SlacState::Unmatched:
        return "Unmatched";
    case SlacState::Matching:
        return "Matching";
    case SlacState::WaitForLink:
        return "WaitForLink";
    case SlacState::Validate:
        return "Validate";
    case SlacState::Matched:
        return "Matched";
    }
    return "Init";
}

SlacState parse_slac_state(std::string const& state_name) {
    if (state_name == "Init") {
        return SlacState::Init;
    }
    if (state_name == "Reset") {
        return SlacState::Reset;
    }
    if (state_name == "ResetChip") {
        return SlacState::ResetChip;
    }
    if (state_name == "Idle") {
        return SlacState::Idle;
    }
    if (state_name == "Failed") {
        return SlacState::Failed;
    }
    if (state_name == "Unmatched") {
        return SlacState::Unmatched;
    }
    if (state_name == "Matching") {
        return SlacState::Matching;
    }
    if (state_name == "WaitForLink") {
        return SlacState::WaitForLink;
    }
    if (state_name == "Validate") {
        return SlacState::Validate;
    }
    if (state_name == "Matched") {
        return SlacState::Matched;
    }
    throw std::out_of_range("Provided string " + state_name + " could not be converted to enum of type " +
                            "everest::lib::slac::SlacState");
}

std::string to_string(D3State state) {
    switch (state) {
    case D3State::Unmatched:
        return "Unmatched";
    case D3State::Matching:
        return "Matching";
    case D3State::Matched:
        return "Matched";
    }
    return "Unmatched";
}

D3State parse_d3_state(std::string const& state_name) {
    if (state_name == "Unmatched") {
        return D3State::Unmatched;
    }
    if (state_name == "Matching") {
        return D3State::Matching;
    }
    if (state_name == "Matched") {
        return D3State::Matched;
    }
    throw std::out_of_range("Provided string " + state_name + " could not be converted to enum of type " +
                            "everest::lib::slac::D3State");
}

SlacTelemetry parse_telemetry(nlohmann::json const& json) {
    SlacTelemetry telemetry;

    if (json.contains("matching_requested")) {
        telemetry.matching_requested = json.at("matching_requested").get<bool>();
    }
    if (json.contains("modem_PIB")) {
        telemetry.modem_PIB = json.at("modem_PIB").get<bool>();
    }
    if (json.contains("modem_NMK")) {
        telemetry.modem_NMK = json.at("modem_NMK").get<bool>();
    }
    if (json.contains("modem_link_ready")) {
        telemetry.modem_link_ready = json.at("modem_link_ready").get<bool>();
    }
    if (json.contains("session_count")) {
        telemetry.session_count = json.at("session_count").get<int>();
    }
    if (json.contains("average_attenuation")) {
        telemetry.average_attenuation = json.at("average_attenuation").get<float>();
    }
    telemetry.ev_mac.fill(0);
    if (json.contains("ev_mac")) {
        auto parsed_mac = parse_mac_addr(json.at("ev_mac").get<std::string>());
        if (parsed_mac) {
            telemetry.ev_mac = *parsed_mac;
        }
    }
    if (json.contains("match_state")) {
        telemetry.match_state = parse_slac_state(json.at("match_state").get<std::string>());
    }
    if (json.contains("d3_state")) {
        telemetry.d3_state = parse_d3_state(json.at("d3_state").get<std::string>());
    }

    return telemetry;
}

} // namespace

std::string serialize(SlacTelemetry val) {
    nlohmann::json json = nlohmann::json{
        {"matching_requested", val.matching_requested},
        {"modem_PIB", val.modem_PIB},
        {"modem_NMK", val.modem_NMK},
        {"modem_link_ready", val.modem_link_ready},
        {"session_count", val.session_count},
        {"average_attenuation", val.average_attenuation},
        {"ev_mac", format_mac_addr(val.ev_mac)},
        {"match_state", to_string(val.match_state)},
        {"d3_state", to_string(val.d3_state)},
    };
    return json.dump(4);
}

SlacTelemetry deserialize(std::string const& val) {
    return parse_telemetry(nlohmann::json::parse(val));
}

} // namespace everest::lib::slac
