// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "UUGreenPower_UR1000X0.hpp"
#include <algorithm>
#include <regex>

namespace module {

static std::vector<std::string> split_by_delimeters(const std::string& s, const std::string& delimeters) {
    std::regex re("[" + delimeters + "]");
    std::sregex_token_iterator first{s.begin(), s.end(), re, -1}, last;
    return {first, last};
}

static std::vector<uint8_t> parse_module_addresses(const std::string a) {
    std::vector<uint8_t> addresses;
    auto adr = split_by_delimeters(a, ",");
    for (const auto& ad : adr) {
        addresses.push_back(std::stoi(ad));
    }
    return addresses;
}

void UUGreenPower_UR1000X0::init() {

    // Install the address filter before opening CAN and enabling communication.
    auto module_addresses = parse_module_addresses(config.module_addresses);
    acdc.set_module_address(module_addresses, config.can_device);
    const bool broadcast =
        std::find(module_addresses.begin(), module_addresses.end(), UU::ADDR_BROADCAST) != module_addresses.end();
    EVLOG_info << "UUGreen [" << config.can_device << "]: module addresses: " << config.module_addresses
               << ", mode: " << (broadcast ? "broadcast" : "individual addresses");

    // Connect measurement and identification handlers before any CAN replies can arrive.
    invoke_init(*p_main);

    if (!acdc.open_device(config.can_device.c_str())) {
        EVLOG_AND_THROW(EVEXCEPTION(Everest::EverestConfigError, "Could not open CAN interface ", config.can_device));
    }

    acdc.set_voltage_mode(config.voltage_mode);

    acdc.switch_on(false);

    acdc.request_module_info();
}

void UUGreenPower_UR1000X0::ready() {
    invoke_ready(*p_main);
}

} // namespace module
