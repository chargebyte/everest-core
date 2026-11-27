// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <AcSessionConfigurationStore.hpp>

namespace module {
namespace {

types::evse_manager::ACChargingSessionConfiguration make_configuration(bool isod20, bool isod2, bool fake_dc) {
    types::evse_manager::ACChargingSessionConfiguration configuration;
    configuration.allow_isod20 = isod20;
    configuration.allow_isod2 = isod2;
    configuration.allow_isod2_fake_dc = fake_dc;
    return configuration;
}

TEST(AcSessionConfigurationStoreTest, UsesTheLongestMatchingMacPrefix) {
    AcSessionConfigurationStore store(make_configuration(true, true, false));

    auto oui_configuration = make_configuration(false, false, true);
    oui_configuration.mac_filter = std::vector<std::string>{"AA:BB:CC"};
    ASSERT_TRUE(store.set(oui_configuration));

    auto full_mac_configuration = make_configuration(true, false, false);
    full_mac_configuration.mac_filter = std::vector<std::string>{"AA:BB:CC:11:22:33"};
    ASSERT_TRUE(store.set(full_mac_configuration));

    EXPECT_TRUE(store.get("aa:bb:cc:11:22:33").configuration.allow_isod20);
    EXPECT_FALSE(store.get("aa:bb:cc:11:22:33").configuration.allow_isod2_fake_dc);
    EXPECT_TRUE(store.get("AA:BB:CC:44:55:66").configuration.allow_isod2_fake_dc);
    EXPECT_TRUE(store.get("DD:EE:FF:11:22:33").manifest_default);
}

TEST(AcSessionConfigurationStoreTest, RejectsInvalidMacFiltersWithoutChangingExistingRules) {
    AcSessionConfigurationStore store(make_configuration(true, true, false));

    auto valid_configuration = make_configuration(false, true, false);
    valid_configuration.mac_filter = std::vector<std::string>{"AA:BB:CC"};
    ASSERT_TRUE(store.set(valid_configuration));

    auto invalid_configuration = make_configuration(true, false, true);
    invalid_configuration.mac_filter = std::vector<std::string>{"11:22:33", "not-a-mac"};
    EXPECT_FALSE(store.set(invalid_configuration));
    EXPECT_TRUE(store.get("AA:BB:CC:11:22:33").configuration.allow_isod2);
    EXPECT_FALSE(store.get("AA:BB:CC:11:22:33").configuration.allow_isod2_fake_dc);
}

TEST(AcSessionConfigurationStoreTest, EmptyMacPolicyKeepsHlcForNegotiation) {
    AcSessionConfigurationStore store(make_configuration(false, false, false));
    const auto empty_protocols = store.protocols_for(store.get_default(), false);
    EXPECT_FALSE(store.requires_hlc(store.get_default(), "", empty_protocols, true));

    auto mac_policy = make_configuration(false, false, false);
    mac_policy.mac_filter = std::vector<std::string>{"AA:BB:CC"};
    ASSERT_TRUE(store.set(mac_policy));
    EXPECT_TRUE(store.requires_hlc(store.get_default(), "", empty_protocols, true));
    EXPECT_FALSE(store.requires_hlc(store.get_default(), "", empty_protocols, false));

    const auto matched = store.get("AA:BB:CC:11:22:33");
    EXPECT_TRUE(store.is_mac_only(matched, "AA:BB:CC:11:22:33", empty_protocols));
    EXPECT_TRUE(store.requires_hlc(matched, "AA:BB:CC:11:22:33", empty_protocols, false));
    const auto unmatched = store.get("DD:EE:FF:11:22:33");
    EXPECT_FALSE(store.is_mac_only(unmatched, "DD:EE:FF:11:22:33", empty_protocols));
    EXPECT_FALSE(store.requires_hlc(unmatched, "DD:EE:FF:11:22:33", empty_protocols, false));
}

TEST(AcSessionConfigurationStoreTest, MacOnlyPolicyDoesNotReinitializeBeforeSapWhenDefaultIsFakeDc) {
    EXPECT_FALSE(AcSessionConfigurationStore::requires_session_reinit(true, false, true, false, true));
    EXPECT_TRUE(AcSessionConfigurationStore::requires_session_reinit(true, false, true, false, false));
}

TEST(AcSessionConfigurationStoreTest, RejectsUnsupportedProtocolPolicyWithoutChangingDefault) {
    AcSessionConfigurationStore store(make_configuration(false, true, false));
    types::iso15118::SupportedAppProtocols available;
    available.app_protocols = {types::iso15118::SupportedAppProtocol::ISO15118D2};
    store.set_available_protocols(available);

    EXPECT_FALSE(store.set(make_configuration(true, false, false)));
    EXPECT_TRUE(store.get_default().configuration.allow_isod2);
    EXPECT_FALSE(store.get_default().configuration.allow_isod20);
}

TEST(AcSessionConfigurationStoreTest, FakeDcExcludesIso20AndAddsDin) {
    AcSessionConfigurationStore store(make_configuration(true, true, false));
    auto policy = make_configuration(true, false, true);
    ASSERT_TRUE(store.set(policy));

    const auto protocols = store.protocols_for(store.get(""));
    ASSERT_EQ(protocols.app_protocols.size(), 1);
    EXPECT_EQ(protocols.app_protocols[0], types::iso15118::SupportedAppProtocol::DIN70121);
}

TEST(AcSessionConfigurationStoreTest, FakeDcCanAlsoOfferIso2WhenConfigured) {
    AcSessionConfigurationStore store(make_configuration(false, true, false));
    auto policy = make_configuration(true, true, true);
    ASSERT_TRUE(store.set(policy));

    const auto protocols = store.protocols_for(store.get(""));
    ASSERT_EQ(protocols.app_protocols.size(), 2);
    EXPECT_EQ(protocols.app_protocols[0], types::iso15118::SupportedAppProtocol::ISO15118D2);
    EXPECT_EQ(protocols.app_protocols[1], types::iso15118::SupportedAppProtocol::DIN70121);
}

TEST(AcSessionConfigurationStoreTest, ManifestDefaultPreservesOnlyHlcCapabilitiesThatAreAvailable) {
    AcSessionConfigurationStore store(make_configuration(true, true, true));
    types::iso15118::SupportedAppProtocols available;
    available.app_protocols = {types::iso15118::SupportedAppProtocol::ISO15118D20,
                               types::iso15118::SupportedAppProtocol::ISO15118D2,
                               types::iso15118::SupportedAppProtocol::DIN70121};
    store.set_available_protocols(available);

    const auto protocols = store.protocols_for(store.get(""));
    ASSERT_EQ(protocols.app_protocols.size(), 2);
    EXPECT_EQ(protocols.app_protocols[0], types::iso15118::SupportedAppProtocol::ISO15118D2);
    EXPECT_EQ(protocols.app_protocols[1], types::iso15118::SupportedAppProtocol::DIN70121);
}

TEST(AcSessionConfigurationStoreTest, ManifestDefaultHonorsProtocolFlagsForAcAndFakeDc) {
    AcSessionConfigurationStore store(make_configuration(false, false, true));
    types::iso15118::SupportedAppProtocols available;
    available.app_protocols = {types::iso15118::SupportedAppProtocol::ISO15118D20,
                               types::iso15118::SupportedAppProtocol::ISO15118D2,
                               types::iso15118::SupportedAppProtocol::DIN70121};
    store.set_available_protocols(available);

    const auto policy = store.get_default();
    EXPECT_TRUE(store.protocols_for(policy, false).app_protocols.empty());
    const auto fake_dc_protocols = store.protocols_for(policy, true);
    ASSERT_EQ(fake_dc_protocols.app_protocols.size(), 1);
    EXPECT_EQ(fake_dc_protocols.app_protocols.front(), types::iso15118::SupportedAppProtocol::DIN70121);
}

TEST(AcSessionConfigurationStoreTest, DoesNotRequestProtocolsTheHlcDoesNotSupport) {
    AcSessionConfigurationStore store(make_configuration(true, true, true));
    types::iso15118::SupportedAppProtocols available;
    available.app_protocols = {types::iso15118::SupportedAppProtocol::ISO15118D20};
    store.set_available_protocols(available);

    auto configuration = make_configuration(true, true, true);
    EXPECT_FALSE(store.set(configuration));
    const auto policy = store.get("");
    const auto fake_dc_protocols = store.protocols_for(policy);
    const auto ac_protocols = store.protocols_for(policy, false);

    EXPECT_TRUE(fake_dc_protocols.app_protocols.empty());
    ASSERT_EQ(ac_protocols.app_protocols.size(), 1);
    EXPECT_EQ(ac_protocols.app_protocols.front(), types::iso15118::SupportedAppProtocol::ISO15118D20);
}

} // namespace
} // namespace module
