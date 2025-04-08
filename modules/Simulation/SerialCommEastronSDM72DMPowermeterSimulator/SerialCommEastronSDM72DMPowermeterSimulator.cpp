// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SerialCommEastronSDM72DMPowermeterSimulator.hpp"

namespace module {

void SerialCommEastronSDM72DMPowermeterSimulator::init() {
    invoke_init(*p_main);
}

void SerialCommEastronSDM72DMPowermeterSimulator::ready() {
    std::thread t([this]() {
        EVLOG_info << "Delaying ready";
        std::this_thread::sleep_for(std::chrono::seconds(5));
        this->delayed_ready = true;
        EVLOG_info << "Delayed ready is available";
    });
    t.detach();

    invoke_ready(*p_main);
}

} // namespace module
