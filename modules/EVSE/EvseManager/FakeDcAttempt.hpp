// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <cstdint>
#include <utility>

namespace module::detail {

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
bool begin_fake_dc_attempt_on_dlink_ready(bool dlink_ready, bool fake_dc_enabled, bool previous_teardown_pending,
                                          bool& attempt_consumed, std::atomic_bool& attempt_active,
                                          std::uint64_t& attempt_generation, std::atomic_bool& soc_received,
                                          std::atomic_bool& timeout_stop_requested,
                                          std::atomic<std::uint64_t>& timeout_generation,
                                          StartTimeout&& start_timeout) {
    if (not dlink_ready or not fake_dc_enabled or previous_teardown_pending or attempt_consumed or attempt_active) {
        return false;
    }

    return begin_fake_dc_attempt(attempt_consumed, attempt_active, attempt_generation, soc_received,
                                 timeout_stop_requested, timeout_generation, std::forward<StartTimeout>(start_timeout));
}

template <typename StopHlc, typename HlcLinkInUse, typename ReturnToBasicAc>
void stop_fake_dc_attempt(StopHlc&& stop_hlc, HlcLinkInUse&& hlc_link_in_use, ReturnToBasicAc&& return_to_basic_ac) {
    std::forward<StopHlc>(stop_hlc)();
    if (not std::forward<HlcLinkInUse>(hlc_link_in_use)()) {
        std::forward<ReturnToBasicAc>(return_to_basic_ac)();
    }
}

} // namespace module::detail
