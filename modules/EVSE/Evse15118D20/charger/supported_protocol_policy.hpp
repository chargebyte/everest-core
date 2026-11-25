// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <algorithm>
#include <utility>
#include <vector>

#include <iso15118/session/protocol.hpp>

namespace module::charger {

struct SupportedProtocolPolicyResult {
    std::vector<iso15118::ProtocolId> protocols;
    bool fake_dc_protocol_available{false};
    bool din_removed{false};
    bool din_removed_for_tls{false};
};

struct SupportedProtocolPolicyOptions {
    bool fake_dc_requested{false};
    bool ac_energy_transfer_mode{false};
    bool supports_iso15118_2{false};
    bool supports_din70121{false};
    bool enforce_tls{false};
};

inline SupportedProtocolPolicyResult
compute_supported_protocols(const std::vector<iso15118::ProtocolId>& configured_protocols,
                            const std::vector<iso15118::ProtocolId>& initial_protocols,
                            const SupportedProtocolPolicyOptions& options) {
    const bool configured_offer_empty = configured_protocols.empty();
    auto offered = configured_offer_empty ? initial_protocols : configured_protocols;
    const auto usable_for_fake_dc = [&options](iso15118::ProtocolId protocol) {
        return protocol == iso15118::ProtocolId::ISO15118_2 or
               (protocol == iso15118::ProtocolId::DIN70121 and not options.enforce_tls);
    };

    // Before ready(), configured_protocols is not populated yet; use module capabilities to decide whether Fake-DC
    // can be applied once the configured offer becomes available.
    const bool fake_dc_protocol_available =
        configured_offer_empty
            ? options.supports_iso15118_2 or (options.supports_din70121 and not options.enforce_tls)
            : std::any_of(configured_protocols.begin(), configured_protocols.end(), usable_for_fake_dc);
    const bool apply_fake_dc = options.fake_dc_requested and fake_dc_protocol_available;
    bool din_removed{false};
    bool din_removed_for_tls{false};

    // Fake-DC is handled by the DIN and ISO 15118-2 state machines, not ISO 15118-20. DIN is not usable
    // for Fake-DC when TLS is enforced, even if ISO 15118-2 is also available.
    if (options.fake_dc_requested and options.enforce_tls) {
        din_removed_for_tls =
            std::find(offered.begin(), offered.end(), iso15118::ProtocolId::DIN70121) != offered.end();
        offered.erase(std::remove(offered.begin(), offered.end(), iso15118::ProtocolId::DIN70121), offered.end());
    }
    if (options.fake_dc_requested) {
        offered.erase(std::remove(offered.begin(), offered.end(), iso15118::ProtocolId::ISO15118_20), offered.end());
    } else if (not configured_offer_empty and options.ac_energy_transfer_mode) {
        const auto din = std::find(offered.begin(), offered.end(), iso15118::ProtocolId::DIN70121);
        if (din != offered.end()) {
            offered.erase(din);
            din_removed = true;
        }
    }

    return {std::move(offered), fake_dc_protocol_available, din_removed, din_removed_for_tls};
}

} // namespace module::charger
