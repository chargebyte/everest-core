// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "supported_protocol_policy.hpp"

namespace {

using iso15118::ProtocolId;
using module::charger::compute_supported_protocols;
using module::charger::SupportedProtocolPolicyOptions;

SupportedProtocolPolicyOptions fake_dc_ac_options() {
    SupportedProtocolPolicyOptions options;
    options.fake_dc_requested = true;
    options.ac_energy_transfer_mode = true;
    return options;
}

SupportedProtocolPolicyOptions ac_options() {
    SupportedProtocolPolicyOptions options;
    options.ac_energy_transfer_mode = true;
    return options;
}

TEST(SupportedProtocolPolicyTest, fake_dc_excludes_iso20_protocol) {
    auto options = fake_dc_ac_options();
    options.supports_iso15118_2 = true;
    options.supports_din70121 = true;
    const auto result = compute_supported_protocols(
        {ProtocolId::DIN70121, ProtocolId::ISO15118_2, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_TRUE(result.fake_dc_protocol_available);
    EXPECT_FALSE(result.din_removed);
    EXPECT_EQ(result.protocols, (std::vector<ProtocolId>{ProtocolId::DIN70121, ProtocolId::ISO15118_2}));
}

TEST(SupportedProtocolPolicyTest, fake_dc_rejects_iso20_without_legacy_protocol) {
    const auto result = compute_supported_protocols({ProtocolId::ISO15118_20}, {}, fake_dc_ac_options());

    EXPECT_FALSE(result.fake_dc_protocol_available);
    EXPECT_TRUE(result.protocols.empty());
}

TEST(SupportedProtocolPolicyTest, fake_dc_keeps_iso2_as_the_only_legacy_protocol) {
    auto options = fake_dc_ac_options();
    options.supports_iso15118_2 = true;
    const auto result = compute_supported_protocols({ProtocolId::ISO15118_2, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_TRUE(result.fake_dc_protocol_available);
    EXPECT_EQ(result.protocols, (std::vector<ProtocolId>{ProtocolId::ISO15118_2}));
}

TEST(SupportedProtocolPolicyTest, enforce_tls_makes_din_unusable_for_fake_dc) {
    auto options = fake_dc_ac_options();
    options.supports_din70121 = true;
    options.enforce_tls = true;
    const auto result = compute_supported_protocols({ProtocolId::DIN70121, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_FALSE(result.fake_dc_protocol_available);
    EXPECT_FALSE(result.din_removed);
    EXPECT_TRUE(result.din_removed_for_tls);
    EXPECT_TRUE(result.protocols.empty());
}

TEST(SupportedProtocolPolicyTest, ac_without_fake_dc_removes_din_but_keeps_other_protocols) {
    auto options = ac_options();
    options.supports_iso15118_2 = true;
    options.supports_din70121 = true;
    const auto result = compute_supported_protocols(
        {ProtocolId::DIN70121, ProtocolId::ISO15118_2, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_TRUE(result.fake_dc_protocol_available);
    EXPECT_TRUE(result.din_removed);
    EXPECT_EQ(result.protocols, (std::vector<ProtocolId>{ProtocolId::ISO15118_2, ProtocolId::ISO15118_20}));
}

TEST(SupportedProtocolPolicyTest, pre_ready_fake_dc_excludes_initial_iso20_offer) {
    SupportedProtocolPolicyOptions options;
    options.fake_dc_requested = true;
    options.supports_iso15118_2 = true;
    const auto result = compute_supported_protocols({}, {ProtocolId::ISO15118_20}, options);

    EXPECT_TRUE(result.fake_dc_protocol_available);
    EXPECT_TRUE(result.protocols.empty());
}

TEST(SupportedProtocolPolicyTest, enforce_tls_removes_din_even_when_iso2_is_available) {
    auto options = fake_dc_ac_options();
    options.supports_iso15118_2 = true;
    options.supports_din70121 = true;
    options.enforce_tls = true;
    const auto result = compute_supported_protocols(
        {ProtocolId::DIN70121, ProtocolId::ISO15118_2, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_TRUE(result.fake_dc_protocol_available);
    EXPECT_FALSE(result.din_removed);
    EXPECT_TRUE(result.din_removed_for_tls);
    EXPECT_EQ(result.protocols, (std::vector<ProtocolId>{ProtocolId::ISO15118_2}));
}

TEST(SupportedProtocolPolicyTest, enforce_tls_does_not_remove_din_without_fake_dc) {
    SupportedProtocolPolicyOptions options;
    options.supports_din70121 = true;
    options.enforce_tls = true;
    const auto result = compute_supported_protocols({ProtocolId::DIN70121, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_FALSE(result.din_removed_for_tls);
    EXPECT_FALSE(result.din_removed);
    EXPECT_EQ(result.protocols, (std::vector<ProtocolId>{ProtocolId::DIN70121, ProtocolId::ISO15118_20}));
}

TEST(SupportedProtocolPolicyTest, dc_only_mode_keeps_din_without_fake_dc) {
    SupportedProtocolPolicyOptions options;
    options.supports_din70121 = true;
    const auto result = compute_supported_protocols({ProtocolId::DIN70121, ProtocolId::ISO15118_20}, {}, options);

    EXPECT_FALSE(result.din_removed);
    EXPECT_EQ(result.protocols, (std::vector<ProtocolId>{ProtocolId::DIN70121, ProtocolId::ISO15118_20}));
}

} // namespace
