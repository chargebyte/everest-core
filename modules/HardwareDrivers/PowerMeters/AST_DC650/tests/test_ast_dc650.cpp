// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 chargebyte GmbH
#include "main/powermeterImpl.hpp"
#include "meter.hpp"
#include "temperature/temperature_sensorImpl.hpp"
#include <atomic>
#include <deque>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <limits>
#include <sys/select.h>
#include <unistd.h>

using namespace ast_app_layer;
namespace {
using C = CommandType;
using R = CommandResult;
Bytes le(std::uint64_t value, std::size_t width) {
    Bytes result;
    for (std::size_t i = 0; i < width; ++i) {
        result.push_back(value & 0xff);
        value >>= 8;
    }
    return result;
}
Bytes bytes(const std::string& value) {
    return Bytes(value.begin(), value.end());
}
const std::string key = "04" + std::string(128, 'a');
const std::string uuid = "12345678-1234-5678-1234-567812345678";
std::string ocmf(const std::string& id = uuid) {
    return "OCMF|{\"TT\":\"tariff<=>" + id +
           "\",\"RD\":[{\"RV\":12.3}]}|{\"SA\":\"ECDSA-secp256r1-SHA256\",\"SD\":\"abcd\"}";
}
types::powermeter::TransactionReq transaction() {
    types::powermeter::TransactionReq req;
    req.evse_id = "EVSE";
    req.transaction_id = uuid;
    req.identification_status = types::powermeter::OCMFUserIdentificationStatus::ASSIGNED;
    req.identification_type = types::powermeter::OCMFIdentificationType::ISO14443;
    req.identification_data = "1234ABCD";
    req.tariff_text = "tariff";
    return req;
}
struct ScriptedTransport : Transport {
    std::map<C, std::deque<Response>> script;
    std::vector<std::vector<Bytes>> calls;
    std::map<C, Bytes> defaults;
    bool fail_start{false};
    void push(C c, Bytes data = {}, R status = R::OK) {
        script[c].push_back({c, status, std::move(data)});
    }
    void setup(bool display = false) {
        defaults[C::CHARGE_POINT_ID] = Bytes(display ? 38 : 14, 0);
        const auto id = bytes("EVSE");
        std::copy(id.begin(), id.end(), defaults[C::CHARGE_POINT_ID].begin() + 1);
        defaults[C::AB_SERVER_ID] = bytes("AST1234567");
        defaults[C::AB_DEVICE_TYPE] = bytes("DC650");
        defaults[C::GET_PUBKEY_STR16] = bytes(key);
        defaults[C::TIME] = {};
        defaults[C::STOP_TRANSACTION] = {};
        defaults[C::START_TRANSACTION] = {};
        defaults[C::GET_DEV_VOLTAGE_L1] = le(75012, 4);
        defaults[C::GET_CURRENT_L1] = le(static_cast<std::uint32_t>(-12345), 4);
        defaults[C::GET_IMPORT_DEV_POWER] = le(12300, 4);
        defaults[C::GET_EXPORT_DEV_POWER] = le(45600, 4);
        defaults[C::GET_TOTAL_IMPORT_DEV_ENERGY] = le(1234567890123ULL, 8);
        defaults[C::GET_TOTAL_EXPORT_DEV_ENERGY] = le(31, 8);
        defaults[C::TEMPERATURE] = le(static_cast<std::uint16_t>(-123), 2);
    }
    std::vector<Response> exchange(const std::vector<Bytes>& commands) override {
        calls.push_back(commands);
        std::vector<Response> result;
        for (const auto& cmd : commands) {
            const auto type = static_cast<C>(unsigned_le(cmd, 0, 2));
            if (fail_start && type == C::START_TRANSACTION)
                throw std::runtime_error("simulated start timeout");
            if (!script[type].empty()) {
                result.push_back(script[type].front());
                script[type].pop_front();
            } else if (defaults.count(type))
                result.push_back({type, R::OK, defaults[type]});
            else
                result.push_back({type, R::LOGICAL_ADDRESS_NOT_FOUND, {}});
        }
        // Responses need not arrive in request order.
        std::reverse(result.begin(), result.end());
        return result;
    }
    std::size_t count(C c) const {
        std::size_t n = 0;
        for (const auto& call : calls)
            for (const auto& cmd : call)
                if (unsigned_le(cmd, 0, 2) == static_cast<unsigned>(c))
                    ++n;
        return n;
    }
};
class MeterTest : public ::testing::Test {
protected:
    ScriptedTransport* transport;
    std::unique_ptr<Meter> meter;
    void setup(bool display = false) {
        auto owned = std::make_unique<ScriptedTransport>();
        transport = owned.get();
        transport->setup(display);
        meter = std::make_unique<Meter>(std::move(owned), "auto", -4);
        meter->initialize();
    }
};
} // namespace

TEST(ASTProtocol, DetectsDocumentedVariantsOnly) {
    EXPECT_EQ(detect_variant(Bytes(14)), ProtocolVariant::dc_meter_v4_9);
    EXPECT_EQ(detect_variant(Bytes(38)), ProtocolVariant::detached_display_v6_5);
    EXPECT_THROW(detect_variant(Bytes(15)), std::runtime_error);
}
TEST(ASTProtocol, Builds49StartWithExactLengthAndTariffMarker) {
    auto req = transaction();
    const auto cmd = start_command(req, ProtocolVariant::dc_meter_v4_9, -4, 1663593379);
    EXPECT_EQ(unsigned_le(cmd, 2, 2), cmd.size());
    EXPECT_EQ(unsigned_le(cmd, 5, 4), 1663593379u);
    EXPECT_EQ(signed_le(cmd, 9, 1), -4);
    EXPECT_EQ(cmd[10], 1);
    EXPECT_EQ(cmd[11], 10);
    EXPECT_EQ(std::string(cmd.begin() + 52, cmd.end()), "#TT#tariff<=>" + uuid + "#TT#");
    EXPECT_EQ(cmd.size(), 60 + 6 + 3 + uuid.size());
}
TEST(ASTProtocol, Builds65StartWithSeparateTerminatedFields) {
    const auto cmd = start_command(transaction(), ProtocolVariant::detached_display_v6_5, 8, 1663593379);
    EXPECT_EQ(unsigned_le(cmd, 2, 2), cmd.size());
    const std::string tail(cmd.begin() + 52, cmd.end());
    EXPECT_EQ(tail, std::string("#TT#tariff\0#TT#", 15) + uuid + '\0');
}
TEST(ASTProtocol, ValidatesByteLimitsWithoutTruncation) {
    auto req = transaction();
    req.tariff_text = std::string(211, 'a'); // 211 + 3 + 36 = 250
    EXPECT_NO_THROW(start_command(req, ProtocolVariant::dc_meter_v4_9, 0, 0));
    req.tariff_text->push_back('a');
    EXPECT_THROW(start_command(req, ProtocolVariant::dc_meter_v4_9, 0, 0), std::invalid_argument);
    req.tariff_text = std::string(250, 'a');
    EXPECT_NO_THROW(start_command(req, ProtocolVariant::detached_display_v6_5, 0, 0));
    req.identification_data = std::string(41, 'a');
    EXPECT_THROW(start_command(req, ProtocolVariant::detached_display_v6_5, 0, 0), std::invalid_argument);
    req.identification_data = std::string("bad\0data", 8);
    EXPECT_THROW(start_command(req, ProtocolVariant::dc_meter_v4_9, 0, 0), std::invalid_argument);
    req.identification_data = "valid";
    req.tariff_text = "#TT#";
    EXPECT_THROW(start_command(req, ProtocolVariant::dc_meter_v4_9, 0, 0), std::invalid_argument);
}
TEST(ASTProtocol, SignedNumbersAndLargeEnergy) {
    EXPECT_EQ(signed_le(le(0x8000000000000000ULL, 8), 0, 8), std::numeric_limits<std::int64_t>::min());
    EXPECT_EQ(signed_le(le(0xffffffffffffffffULL, 8), 0, 8), -1);
    EXPECT_EQ(signed_le(le(0xffffffff, 4), 0, 4), -1);
    EXPECT_DOUBLE_EQ(unsigned_le(le(1234567890123ULL, 8), 0, 8) / 10.0, 123456789012.3);
    EXPECT_THROW(unsigned_le(Bytes{1}, 0, 2), std::runtime_error);
}
TEST(ASTProtocol, RejectsMalformedResponseHeadersAndStatuses) {
    EXPECT_THROW(parse_responses({}, 1), std::runtime_error);
    EXPECT_THROW(parse_responses({2, 1, 21, 5, 0, 0}, 1), std::runtime_error);
    EXPECT_THROW(parse_responses({1, 1, 21, 0, 0, 0}, 1), std::runtime_error);
    EXPECT_THROW(parse_responses({1, 1, 21, 6, 0, 0}, 1), std::runtime_error);
    EXPECT_THROW(parse_responses({1, 1, 21, 5, 0, 10}, 1), std::runtime_error);
    EXPECT_THROW(parse_responses({1, 1, 21, 5}, 1), std::runtime_error);
}
TEST(ASTProtocol, KeyIsAsciiHexAndOcmfIsPreserved) {
    EXPECT_EQ(public_key(bytes(key)), key);
    EXPECT_THROW(public_key(bytes("04zz")), std::runtime_error);
    EXPECT_EQ(validate_ocmf(bytes(ocmf()), uuid, false).text, ocmf());
    EXPECT_THROW(validate_ocmf(bytes(ocmf("other")), uuid, false), std::runtime_error);
    EXPECT_THROW(validate_ocmf({}, uuid, true), std::runtime_error);
    EXPECT_THROW(validate_ocmf(bytes("OCMF|{}|{}"), uuid, true), std::runtime_error);
    EXPECT_THROW(validate_ocmf(bytes(ocmf() + "junk"), uuid, true), std::runtime_error);
}
TEST(ASTSlip, PdfStopExampleAndEscapedAddressAndPayload) {
    slip_protocol::SlipProtocol slip;
    const Bytes expected{0xc0, 0xff, 2, 0x15, 5, 0, 0, 0x2e, 0x5c, 0xc0};
    EXPECT_EQ(slip.package_single(0xff, command(C::STOP_TRANSACTION)), expected);
    for (const auto address : {0xc0, 0xdb, 1}) {
        auto frame = slip.package_single(address, {0xc0, 0xdb});
        EXPECT_EQ(slip.unpack(frame, address), slip_protocol::SlipReturnStatus::SLIP_OK);
        EXPECT_EQ(slip.retrieve_single_message(), (Bytes{static_cast<std::uint8_t>(address), 0xc0, 0xdb}));
    }
}
TEST(ASTSlip, EscapesCrcAndHandlesFragmentsAndConsecutiveFrames) {
    slip_protocol::SlipProtocol slip;
    bool crc_escape_found = false;
    for (unsigned i = 0; i < 65536; ++i) {
        Bytes raw{1, static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(i >> 8)};
        const auto crc = calculate_xModem_crc16(raw);
        if ((crc & 0xff) != 0xc0 && (crc & 0xff) != 0xdb && (crc >> 8) != 0xc0 && (crc >> 8) != 0xdb)
            continue;
        const auto frame = slip.package_single(1, Bytes(raw.begin() + 1, raw.end()));
        for (auto byte : frame) {
            Bytes fragment{byte};
            EXPECT_EQ(slip.unpack(fragment, 1), slip_protocol::SlipReturnStatus::SLIP_OK);
        }
        EXPECT_EQ(slip.retrieve_single_message(), raw);
        crc_escape_found = true;
        break;
    }
    EXPECT_TRUE(crc_escape_found);
    auto a = slip.package_single(1, {1});
    const auto b = slip.package_single(1, {2});
    a.insert(a.end(), b.begin(), b.end());
    slip.unpack(a, 1);
    EXPECT_EQ(slip.retrieve_single_message(), (Bytes{1, 1}));
    EXPECT_EQ(slip.retrieve_single_message(), (Bytes{1, 2}));
}
TEST(ASTSlip, LargeFrameWrongAddressAndCorruption) {
    slip_protocol::SlipProtocol slip;
    const Bytes payload(5000, 0xdb);
    auto frame = slip.package_single(1, payload);
    EXPECT_GT(frame.size(), 10000u);
    EXPECT_EQ(slip.unpack(frame, 1), slip_protocol::SlipReturnStatus::SLIP_OK);
    const auto message = slip.retrieve_single_message();
    EXPECT_EQ(message.size(), 5001u);
    auto wrong = slip.package_single(2, {1});
    slip.unpack(wrong, 1);
    EXPECT_EQ(slip.get_message_counter(), 0u);
    auto corrupt = slip.package_single(1, {1});
    corrupt[2] ^= 1;
    EXPECT_EQ(slip.unpack(corrupt, 1), slip_protocol::SlipReturnStatus::SLIP_ERROR_CRC_MISMATCH);
    Bytes invalid{0xc0, 1, 0xdb, 0x99, 0xc0};
    EXPECT_EQ(slip.unpack(invalid, 1), slip_protocol::SlipReturnStatus::SLIP_ERROR_MALFORMED);
}
TEST_F(MeterTest, PollsEnergyWithoutDiagnosticsAndReturnsTemperature) {
    setup();
    const auto values = meter->sample();
    EXPECT_FLOAT_EQ(*values.current_A->DC, -12.345f);
    EXPECT_DOUBLE_EQ(values.power_W->total, -333.0);
    EXPECT_FLOAT_EQ(*values.voltage_V->DC, 750.12f);
    EXPECT_FLOAT_EQ(values.energy_Wh_import.total, 123456789012.3f);
    EXPECT_FLOAT_EQ(values.energy_Wh_export->total, 3.1f);
    ASSERT_TRUE(values.temperatures);
    EXPECT_FLOAT_EQ(values.temperatures->front().temperature, -12.3f);
    EXPECT_EQ(values.temperatures->front().identification, "meter_internal");
    EXPECT_FALSE(values.timestamp.empty());
}
TEST_F(MeterTest, OptionalTemperatureFailureNeverFabricatesZero) {
    setup();
    transport->push(C::TEMPERATURE, {}, R::LOGICAL_ADDRESS_NOT_FOUND);
    EXPECT_FALSE(meter->sample().temperatures);
    EXPECT_FALSE(meter->sample().temperatures);
    EXPECT_EQ(transport->count(C::TEMPERATURE), 1u);
}
TEST_F(MeterTest, TransientTemperatureFailureRecoversAndMandatoryFailureRejectsDataset) {
    setup();
    transport->push(C::TEMPERATURE, {}, R::NOT_AVAILABLE);
    EXPECT_FALSE(meter->sample().temperatures);
    EXPECT_TRUE(meter->sample().temperatures);
    transport->push(C::GET_TOTAL_IMPORT_DEV_ENERGY, {}, R::NOT_AVAILABLE);
    EXPECT_THROW(meter->sample(), std::runtime_error);
}
TEST_F(MeterTest, StartStop49AndRepeatedRequests) {
    setup();
    EXPECT_THROW(meter->start(transaction()), std::runtime_error);
    EXPECT_FALSE(meter->stop(""));
    meter->start(transaction());
    meter->start(transaction());
    EXPECT_EQ(transport->count(C::START_TRANSACTION), 1u);
    auto other = transaction();
    other.transaction_id = "different";
    EXPECT_THROW(meter->start(other), std::runtime_error);
    EXPECT_THROW(meter->stop("different"), std::runtime_error);
    transport->push(C::GET_LAST_OCMF, bytes(ocmf()));
    const auto value = meter->stop(uuid);
    ASSERT_TRUE(value);
    EXPECT_EQ(value->signed_meter_data, ocmf());
    EXPECT_EQ(value->public_key, key);
    EXPECT_EQ(value->signing_method, "ECDSA-secp256r1-SHA256");
    EXPECT_EQ(meter->stop(uuid)->signed_meter_data, ocmf());
    EXPECT_EQ(transport->count(C::STOP_TRANSACTION), 2u);
}
TEST_F(MeterTest, DisplayFindsOcmfByIdAndRetriesOnlyReadout) {
    setup(true);
    meter->stop("");
    meter->start(transaction());
    transport->push(C::FIND_TRANSACTION_OCMF, {}, R::BUSY);
    transport->push(C::FIND_TRANSACTION_OCMF, {}, R::NOT_AVAILABLE);
    transport->push(C::FIND_TRANSACTION_OCMF, {}, R::BUSY);
    transport->push(C::FIND_TRANSACTION_OCMF, bytes(ocmf("native-id-is-in-request")));
    EXPECT_TRUE(meter->stop(uuid));
    EXPECT_EQ(transport->count(C::STOP_TRANSACTION), 2u);
    EXPECT_EQ(transport->count(C::FIND_TRANSACTION_OCMF), 4u);
}
TEST_F(MeterTest, LookupFallbackRequiresSignedIdAndStaleOcmfFails) {
    setup(true);
    meter->stop("");
    meter->start(transaction());
    transport->push(C::FIND_TRANSACTION_OCMF, {}, R::LOGICAL_ADDRESS_NOT_FOUND);
    transport->push(C::GET_LAST_OCMF, bytes(ocmf("stale")));
    EXPECT_THROW(meter->stop(uuid), std::runtime_error);
    transport->push(C::GET_LAST_OCMF, bytes(ocmf()));
    EXPECT_TRUE(meter->stop(uuid));
    EXPECT_EQ(transport->count(C::STOP_TRANSACTION), 2u);
}
TEST_F(MeterTest, StartTimeoutRequiresCleanupAndIsNotRetried) {
    setup(true);
    meter->stop("");
    transport->fail_start = true;
    EXPECT_THROW(meter->start(transaction()), std::runtime_error);
    EXPECT_THROW(meter->start(transaction()), std::runtime_error);
    EXPECT_EQ(transport->count(C::START_TRANSACTION), 1u);
    meter->stop("");
    transport->fail_start = false;
    EXPECT_NO_THROW(meter->start(transaction()));
}
TEST_F(MeterTest, CleanupRequiresSpecificNoTransactionError) {
    setup();
    transport->push(C::STOP_TRANSACTION, {}, R::NOT_AVAILABLE);
    auto errors = Bytes(50, 0);
    const auto id = le(0x06011627, 4);
    std::copy(id.begin(), id.end(), errors.begin());
    transport->push(C::GET_ERRORS, errors);
    EXPECT_FALSE(meter->stop(""));
    transport->push(C::STOP_TRANSACTION, {}, R::REJECTED);
    transport->push(C::GET_ERRORS, Bytes(50, 0));
    EXPECT_THROW(meter->stop(""), std::runtime_error);
}

TEST_F(MeterTest, DiagnosticSnapshotPreservesFractionalEnergyAndSigned64BitPower) {
    setup();
    meter->sample();
    transport->defaults[C::TIME] = Bytes(5, 0);
    transport->defaults[C::GET_TOTAL_DEV_POWER] = le(static_cast<std::uint64_t>(-1234567890123LL), 8);
    transport->defaults[C::GET_TOTAL_IMPORT_MAINS_ENERGY] = le(1234567890123ULL, 8);
    const auto snapshot = meter->diagnostics(true, false);
    EXPECT_DOUBLE_EQ(snapshot.device_data["total_dev_import_energy_Wh"].get<double>(), 123456789012.3);
    EXPECT_DOUBLE_EQ(snapshot.device_data["total_mains_import_energy_Wh"].get<double>(), 123456789012.3);
    EXPECT_DOUBLE_EQ(snapshot.device_data["total_device_power_W"].get<double>(), -12345678901.23);
}

TEST_F(MeterTest, PublishesSameTemperatureOnBothInterfacesAndOmitsFailedTemperature) {
    setup();
    Everest::ModuleAdapter adapter{};
    adapter.get_error_manager_impl = [](const std::string&) {
        return std::shared_ptr<Everest::error::ErrorManagerImpl>{};
    };
    adapter.get_error_state_monitor_impl = [](const std::string&) {
        return std::shared_ptr<Everest::error::ErrorStateMonitor>{};
    };
    adapter.get_error_factory = [](const std::string&) { return std::shared_ptr<Everest::error::ErrorFactory>{}; };
    adapter.get_mapping = [] { return std::optional<ModuleTierMappings>{}; };
    std::vector<std::tuple<std::string, std::string, nlohmann::json>> publications;
    adapter.publish = [&](const auto& interface, const auto& variable, const auto& value) {
        publications.emplace_back(interface, variable, value);
    };
    ModuleInfo module_info{};
    Everest::MqttProvider mqtt(adapter);
    Everest::PtrContainer<module::AST_DC650> pointer;
    module::temperature::Conf temp_conf;
    auto temperature = std::make_unique<module::temperature::temperature_sensorImpl>(&adapter, pointer, temp_conf);
    module::Conf module_conf;
    module::AST_DC650 module(module_info, mqtt, nullptr, std::move(temperature), module_conf);
    pointer.set(&module);
    module::main::Conf conf{};
    struct Publisher : module::main::powermeterImpl {
        using powermeterImpl::powermeterImpl;
        using powermeterImpl::publish_measurement;
    } publisher(&adapter, pointer, conf);
    publisher.publish_measurement(meter->sample());
    ASSERT_EQ(publications.size(), 2u);
    EXPECT_EQ(std::get<0>(publications[0]), "main");
    EXPECT_EQ(std::get<1>(publications[0]), "powermeter");
    EXPECT_EQ(std::get<0>(publications[1]), "temperature");
    EXPECT_EQ(std::get<1>(publications[1]), "temperatures");
    EXPECT_EQ(std::get<2>(publications[0])["temperatures"], std::get<2>(publications[1]));
    transport->push(C::TEMPERATURE, {}, R::NOT_AVAILABLE);
    publisher.publish_measurement(meter->sample());
    ASSERT_EQ(publications.size(), 3u);
    EXPECT_FALSE(std::get<2>(publications[2]).contains("temperatures"));
}

TEST(ASTMeterConcurrency, SerializesPollingDiagnosticsAndTransactionRequests) {
    struct ConcurrentTransport : ScriptedTransport {
        std::atomic<int> active{0};
        std::atomic<int> maximum{0};
        std::vector<Response> exchange(const std::vector<Bytes>& requests) override {
            const auto current = ++active;
            maximum.store(std::max(maximum.load(), current));
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            auto result = ScriptedTransport::exchange(requests);
            --active;
            return result;
        }
    };
    auto owned = std::make_unique<ConcurrentTransport>();
    auto* transport = owned.get();
    transport->setup(true);
    transport->defaults[C::TIME] = {};
    Meter meter(std::move(owned), "auto", 0);
    meter.initialize();
    meter.stop("");
    meter.start(transaction());
    transport->defaults[C::TIME] = Bytes(5, 0);
    transport->defaults[C::GET_ERRORS] = Bytes(50, 0);
    transport->defaults[C::FIND_TRANSACTION_OCMF] = bytes(ocmf());
    auto poll = std::async(std::launch::async, [&] {
        for (int i = 0; i < 3; ++i)
            meter.sample();
    });
    auto diagnostics = std::async(std::launch::async, [&] { meter.diagnostics(true, true); });
    auto stop = std::async(std::launch::async, [&] { EXPECT_TRUE(meter.stop(uuid)); });
    poll.get();
    diagnostics.get();
    stop.get();
    EXPECT_EQ(transport->maximum, 1);
}

namespace {
struct PseudoTerminal {
    int fd;
    std::string port;
    PseudoTerminal() {
        fd = posix_openpt(O_RDWR | O_NOCTTY);
        if (fd < 0 || grantpt(fd) != 0 || unlockpt(fd) != 0)
            throw std::runtime_error("Cannot create pseudoterminal");
        port = ptsname(fd);
    }
    ~PseudoTerminal() {
        close(fd);
    }
    Bytes receive() {
        Bytes bytes;
        slip_protocol::SlipProtocol slip;
        for (unsigned i = 0; i < 30; ++i) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(fd, &set);
            timeval timeout{0, 100000};
            if (select(fd + 1, &set, nullptr, nullptr, &timeout) <= 0)
                continue;
            std::uint8_t buffer[512];
            const auto size = read(fd, buffer, sizeof(buffer));
            if (size <= 0)
                continue;
            Bytes fragment(buffer, buffer + size);
            bytes.insert(bytes.end(), fragment.begin(), fragment.end());
            slip.unpack(fragment, 0xff);
            if (slip.get_message_counter())
                return bytes;
        }
        throw std::runtime_error("No request on pseudoterminal");
    }
    void send(const Bytes& data) {
        std::size_t offset = 0;
        while (offset < data.size()) {
            const auto count = write(fd, data.data() + offset, data.size() - offset);
            if (count <= 0)
                throw std::runtime_error("Cannot write pseudoterminal response");
            offset += count;
        }
    }
};
} // namespace

TEST(ASTSerialTransport, KeepsReplyFollowingEchoAndReassemblesLargeFragmentedFrame) {
    PseudoTerminal peer;
    SerialTransport transport(peer.port, 9600, 0, 0xc0, true);
    const auto request = command(C::GET_LAST_OCMF);
    auto responder = std::async(std::launch::async, [&] {
        const auto echo = peer.receive();
        slip_protocol::SlipProtocol slip;
        auto response = slip.package_single(0xc0, command(C::GET_LAST_OCMF, Bytes(4995, 0xdb)));
        Bytes prefix = echo;
        prefix.insert(prefix.end(), response.begin(), response.begin() + 3);
        peer.send(prefix);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        peer.send(Bytes(response.begin() + 3, response.end()));
    });
    const auto result = transport.exchange({request});
    responder.get();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result.front().data, Bytes(4995, 0xdb));
}

TEST(ASTSerialTransport, RejectsWrongCommandAndMissingReply) {
    PseudoTerminal peer;
    SerialTransport transport(peer.port, 115200, 0, 1, false);
    auto responder = std::async(std::launch::async, [&] {
        peer.receive();
        slip_protocol::SlipProtocol slip;
        peer.send(slip.package_single(1, command(C::START_TRANSACTION)));
    });
    EXPECT_THROW(transport.exchange({command(C::STOP_TRANSACTION)}), std::runtime_error);
    responder.get();
    EXPECT_THROW(transport.exchange({command(C::STOP_TRANSACTION)}), std::runtime_error);
}

TEST(ASTProtocol, MapsEveryIdentificationTypeExplicitly) {
    using I = types::powermeter::OCMFIdentificationType;
    const std::vector<std::pair<I, unsigned>> mappings{
        {I::NONE, 0},     {I::DENIED, 1},        {I::UNDEFINED, 2},  {I::ISO14443, 10}, {I::ISO15693, 11},
        {I::EMAID, 20},   {I::EVCCID, 21},       {I::EVCOID, 30},    {I::ISO7812, 40},  {I::CARD_TXN_NR, 50},
        {I::CENTRAL, 60}, {I::CENTRAL_1, 61},    {I::CENTRAL_2, 62}, {I::LOCAL, 70},    {I::LOCAL_1, 71},
        {I::LOCAL_2, 72}, {I::PHONE_NUMBER, 80}, {I::KEY_CODE, 90}};
    auto value = transaction();
    for (const auto& [type, number] : mappings) {
        value.identification_type = type;
        const auto start = start_command(value, ProtocolVariant::dc_meter_v4_9, 0, 0);
        EXPECT_EQ(start[11], number);
    }
}

TEST(ASTMeterInitialization, ValidatesExplicitVariantAndNeverProbesWithStart) {
    auto owned = std::make_unique<ScriptedTransport>();
    auto* transport = owned.get();
    transport->setup(true);
    Meter meter(std::move(owned), "dc_meter_v4_9", 0);
    EXPECT_THROW(meter.initialize(), std::runtime_error);
    EXPECT_EQ(transport->count(C::START_TRANSACTION), 0u);
    EXPECT_EQ(transport->count(C::TIME), 0u);
}

TEST(ASTSerialTransport, AcceptsReplyAfterOld1100MillisecondLimit) {
    PseudoTerminal peer;
    SerialTransport transport(peer.port, 115200, 0, 1, false);
    auto responder = std::async(std::launch::async, [&] {
        peer.receive();
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        slip_protocol::SlipProtocol slip;
        peer.send(slip.package_single(1, command(C::STOP_TRANSACTION)));
    });
    EXPECT_EQ(transport.exchange({command(C::STOP_TRANSACTION)}).size(), 1u);
    responder.get();
}
