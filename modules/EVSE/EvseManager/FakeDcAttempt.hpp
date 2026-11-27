// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace module::detail {

inline bool should_terminate_fake_dc_slac_link(bool link_termination_already_requested, bool hlc_link_in_use,
                                               bool slac_unmatched) {
    return not link_termination_already_requested and not hlc_link_in_use and not slac_unmatched;
}

template <typename StartFakeDcFallback, typename HandleChargerLinkLoss, typename NotifySlacLinkLoss>
void handle_fake_dc_link_loss(bool fake_dc_attempt_active, StartFakeDcFallback&& start_fake_dc_fallback,
                              HandleChargerLinkLoss&& handle_charger_link_loss,
                              NotifySlacLinkLoss&& notify_slac_link_loss) {
    if (fake_dc_attempt_active) {
        std::forward<StartFakeDcFallback>(start_fake_dc_fallback)();
    }
    std::forward<HandleChargerLinkLoss>(handle_charger_link_loss)();
    std::forward<NotifySlacLinkLoss>(notify_slac_link_loss)();
}

inline bool should_apply_basic_ac_after_rejected_fake_dc_reinit(bool fallback_pending, bool reinit_rejected,
                                                                bool slac_unmatched) {
    return fallback_pending and reinit_rejected and slac_unmatched;
}

template <typename ApplyBasicAc>
bool apply_basic_ac_after_rejected_fake_dc_reinit(bool fallback_pending, bool reinit_rejected, bool slac_unmatched,
                                                  ApplyBasicAc&& apply_basic_ac) {
    if (not should_apply_basic_ac_after_rejected_fake_dc_reinit(fallback_pending, reinit_rejected, slac_unmatched)) {
        return false;
    }
    std::forward<ApplyBasicAc>(apply_basic_ac)();
    return true;
}

inline bool claim_fake_dc_timeout(std::atomic_bool& fake_dc_enabled, std::atomic_bool& attempt_active,
                                  std::atomic_bool& soc_received, std::atomic_bool& timeout_stop_requested,
                                  std::atomic<std::uint64_t>& timeout_generation, std::uint64_t generation) {
    auto expected_generation = generation;
    return timeout_generation.compare_exchange_strong(expected_generation, 0) and fake_dc_enabled.load() and
           attempt_active and not soc_received and not timeout_stop_requested.exchange(true);
}

template <typename StartTimeout>
bool begin_fake_dc_attempt(bool& attempt_consumed, std::atomic_bool& attempt_active, std::uint64_t& attempt_generation,
                           std::atomic_bool& soc_received, std::atomic_bool& timeout_stop_requested,
                           std::atomic<std::uint64_t>& timeout_generation, StartTimeout&& start_timeout) {
    if (attempt_consumed) {
        return false;
    }

    if (not attempt_active) {
        ++attempt_generation;
    }
    attempt_active = true;
    attempt_consumed = true;
    soc_received = false;
    timeout_stop_requested = false;
    timeout_generation = std::forward<StartTimeout>(start_timeout)();
    return true;
}

template <typename StartTimeout>
bool begin_fake_dc_attempt_on_dlink_ready(bool dlink_ready, bool fake_dc_enabled, bool& attempt_consumed,
                                          std::atomic_bool& attempt_active, std::uint64_t& attempt_generation,
                                          std::atomic_bool& soc_received, std::atomic_bool& timeout_stop_requested,
                                          std::atomic<std::uint64_t>& timeout_generation,
                                          StartTimeout&& start_timeout) {
    if (not dlink_ready or not fake_dc_enabled or attempt_consumed or attempt_active) {
        return false;
    }

    return begin_fake_dc_attempt(attempt_consumed, attempt_active, attempt_generation, soc_received,
                                 timeout_stop_requested, timeout_generation, std::forward<StartTimeout>(start_timeout));
}

} // namespace module::detail
