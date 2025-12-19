// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "slacImpl.hpp"

namespace module {
namespace evse {

using util::State;

void slacImpl::init() {
}

void slacImpl::ready() {
    publish_state(state_to_string(state));
}

void slacImpl::handle_reset(bool& enable) {
    set_state_to_unmatched();
}

bool slacImpl::handle_enter_bcd() {
    set_state_to_matching();
    return true;
}

bool slacImpl::handle_leave_bcd() {
    set_state_to_unmatched();
    return true;
}

bool slacImpl::handle_dlink_terminate() {
    set_state_to_unmatched();
    return true;
}

bool slacImpl::handle_dlink_error() {
    set_state_to_unmatched();
    return true;
}

bool slacImpl::handle_dlink_pause() {
    return true;
}

void slacImpl::set_state_to_unmatched() {
    if (state != State::UNMATCHED) {
        stop_slac_init_timer();
        state = State::UNMATCHED;
        publish_state(state_to_string(state));
        publish_dlink_ready(false);
    }
}

void slacImpl::set_state_to_matching() {
    state = State::MATCHING;
    mod->cntmatching = 0;
    publish_state(state_to_string(state));
    publish_ev_mac_address(mod->config.ev_mac_address);
    start_slac_init_timer();
}

State slacImpl::get_state() const {
    return state;
}

void slacImpl::set_state_matched() {
    stop_slac_init_timer();
    state = State::MATCHED;
    publish_state(state_to_string(state));
    publish_dlink_ready(true);
};

void slacImpl::tick() {
    check_slac_init_timer();
}

void slacImpl::start_slac_init_timer() {
    if (mod->config.slac_init_timeout_ms <= 0) {
        slac_init_timer_active = false;
        return;
    }

    slac_init_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(mod->config.slac_init_timeout_ms);
    slac_init_timer_active = true;
}

void slacImpl::stop_slac_init_timer() {
    slac_init_timer_active = false;
}

void slacImpl::check_slac_init_timer() {
    if (!slac_init_timer_active || state != State::MATCHING) {
        return;
    }

    if (std::chrono::steady_clock::now() >= slac_init_timeout) {
        slac_init_timer_active = false;
        publish_request_error_routine(nullptr);
    }
}
} // namespace evse
} // namespace module
