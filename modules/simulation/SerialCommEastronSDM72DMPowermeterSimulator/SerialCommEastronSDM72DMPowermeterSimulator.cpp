// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "SerialCommEastronSDM72DMPowermeterSimulator.hpp"

namespace module {

void SerialCommEastronSDM72DMPowermeterSimulator::init() {
    invoke_init(*p_main);
}

void SerialCommEastronSDM72DMPowermeterSimulator::ready() {
    invoke_ready(*p_main);
}

} // namespace module
