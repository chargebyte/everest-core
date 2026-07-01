// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include "fsm_controller.hpp"

#include <everest/io/event/fd_event_handler.hpp>

FSMController::FSMController(slac::fsm::evse::Context& context) : ctx(context), fsm(ctx) {
}

void FSMController::init() {
    ctx.log_info("Starting the SLAC state machine");
    active.store(true);
    fsm.restart_fsm();
    m_retrigger.set_timeout_ms(10);
}

void FSMController::stop() {
    active.store(false);
    m_retrigger.set_timeout_ns(0);
}

void FSMController::signal_new_slac_message(slac::messages::HomeplugMessage const& msg) {
    if (!active.load()) {
        return;
    }
    ctx.slac_message_payload = msg;
    fsm.message(msg);
}

void FSMController::signal_reset() {
    if (!active.load()) {
        return;
    }
    m_reset.notify();
}

bool FSMController::signal_enter_bcd() {
    if (!active.load()) {
        return false;
    }
    m_enter_bcd.notify();
    return true;
}

bool FSMController::signal_leave_bcd() {
    if (!active.load()) {
        return false;
    }
    m_leave_bcd.notify();
    return true;
}

void FSMController::handle_retrigger() {
    if (!active.load()) {
        return;
    }
    fsm.update();
}

void FSMController::handle_reset() {
    if (!active.load()) {
        return;
    }
    ctx.log_info("Signal reset");
    fsm.reset();
}

void FSMController::handle_enter_bcd() {
    if (!active.load()) {
        return;
    }
    ctx.log_info("Signal enter_bcd");
    fsm.enter_bcd();
}

void FSMController::handle_leave_bcd() {
    if (!active.load()) {
        return;
    }
    ctx.log_info("Signal leave_bcd");
    fsm.leave_bcd();
}

bool FSMController::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result &= handler.register_event_handler(&m_reset, [this](auto const&) { this->handle_reset(); });
    result &= handler.register_event_handler(&m_enter_bcd, [this](auto const&) { this->handle_enter_bcd(); });
    result &= handler.register_event_handler(&m_leave_bcd, [this](auto const&) { this->handle_leave_bcd(); });
    result &= handler.register_event_handler(&m_retrigger, [this](auto const&) { this->handle_retrigger(); });
    return result;
}

bool FSMController::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result &= handler.unregister_event_handler(&m_reset);
    result &= handler.unregister_event_handler(&m_enter_bcd);
    result &= handler.unregister_event_handler(&m_leave_bcd);
    result &= handler.unregister_event_handler(&m_retrigger);
    return result;
}

void FSMController::run() {
}
