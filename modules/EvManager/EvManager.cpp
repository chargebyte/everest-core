// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvManager.hpp"
#include "main/car_simulatorImpl.hpp"

namespace module {

void EvManager::init() {
    invoke_init(*p_main);
    invoke_init(*p_ev_manager);
}

void EvManager::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_ev_manager);
}

} // namespace module
