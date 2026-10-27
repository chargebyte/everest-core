// SPDX-License-Identifier: Apache-2.0
#include "UUGreenCanDevice.hpp"
#include <algorithm>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/sync_frontend.hpp>
#include <boost/log/sinks/text_ostream_backend.hpp>
#include <boost/make_shared.hpp>
#include <chrono>
#include <condition_variable>
#include <everest/logging.hpp>
#include <gtest/gtest.h>
#include <mutex>
#include <sstream>

namespace {
std::mutex sent_mutex;
std::condition_variable sent_changed;
struct SentFrame {
    uint8_t address;
    UU::CommandType command;
};
std::vector<SentFrame> sent_frames;
} // namespace

// Linker wrapper for CanDevice::_tx; production uses the unchanged SocketCAN transport.
extern "C" bool __wrap__ZN9CanDevice3_txEjRKSt6vectorIhSaIhEE(CanDevice*, uint32_t id,
                                                              const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(sent_mutex);
    sent_frames.push_back({UU::module_address_from_can_id(id), UU::Packet(payload).command_type});
    sent_changed.notify_all();
    return true;
}

class TestDevice : public UUGreenCanDevice {
public:
    using UUGreenCanDevice::rx_handler;
    void receive(uint8_t address, UU::MessageType type, UU::CommandType command, uint32_t data) {
        rx_handler(CAN_EFF_FLAG | UU::encode_can_id(address), UU::Packet(type, command, data));
    }
};

TEST(UUGreen, IgnoresUnconfiguredModulesBeforeParsing) {
    TestDevice device;
    device.set_module_address({1, 2});
    int measurements = 0, identities = 0;
    device.signal_voltage_current.connect([&](float, float) { ++measurements; });
    device.signal_serial_number.connect([&](int, const std::string&) { ++identities; });
    for (auto command : {UU::CommandType::Vout, UU::CommandType::Iout_slow, UU::CommandType::ReadSN})
        device.receive(3, UU::MessageType::ReadDataResponse, command, 1000);
    device.receive(3, UU::MessageType::ReadSerialNumberResponse, UU::CommandType::ReadSN, 123);
    device.rx_handler(CAN_EFF_FLAG | UU::encode_can_id(3), {});
    EXPECT_TRUE(device.telemetries.empty());
    EXPECT_EQ(device.total_current, 0);
    EXPECT_EQ(measurements, 0);
    EXPECT_EQ(identities, 0);
}

TEST(UUGreen, AggregatesOnlyConfiguredModules) {
    TestDevice device;
    device.set_module_address({1, 2});
    float voltage = 0, current = 0;
    device.signal_voltage_current.connect([&](float v, float i) {
        voltage = v;
        current = i;
    });
    device.receive(1, UU::MessageType::ReadDataResponse, UU::CommandType::Iout_slow, 10000);
    device.receive(2, UU::MessageType::ReadDataResponse, UU::CommandType::Iout_slow, 20000);
    device.receive(3, UU::MessageType::ReadDataResponse, UU::CommandType::Iout_slow, 900000);
    device.receive(1, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 400000);
    device.receive(3, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 900000);
    device.receive(2, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 420000);
    EXPECT_FLOAT_EQ(voltage, 410);
    EXPECT_FLOAT_EQ(current, 30);
    EXPECT_EQ(device.telemetries.size(), 2);
}

TEST(UUGreen, ReportsIdentificationOnlyOnFirstResponseOrChange) {
    TestDevice device;
    device.set_module_address({1, 2});
    int identities = 0;
    std::string identification;
    device.signal_serial_number.connect([&](int address, const std::string& text) {
        EXPECT_EQ(address, 1);
        ++identities;
        identification = text;
    });
    auto response = std::vector<uint8_t>(UU::Packet(UU::MessageType::ReadDataResponse, UU::CommandType::ReadSN, 123));
    response[2] = 0x08; // OV type code 2
    response[3] = 0x05; // module type 5
    auto id = CAN_EFF_FLAG | UU::encode_can_id(1);
    device.rx_handler(id, response);
    device.rx_handler(id, response);
    response[0] = static_cast<uint8_t>(UU::MessageType::ReadSerialNumberResponse);
    device.rx_handler(id, response);
    EXPECT_EQ(identities, 1);
    EXPECT_NE(identification.find("Module type: 5"), std::string::npos);
    EXPECT_NE(identification.find("OV Type code: 2"), std::string::npos);
    EXPECT_NE(identification.find("Protocol S/N (differs from label): 123"), std::string::npos);
    response[7] = 124;
    device.rx_handler(id, response);
    EXPECT_EQ(identities, 2);
}

TEST(UUGreen, BroadcastAcceptsAllSources) {
    for (auto addresses : {std::vector<uint8_t>{0}, std::vector<uint8_t>{0, 1}}) {
        TestDevice device;
        device.set_module_address(addresses);
        device.receive(1, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 1000);
        device.receive(127, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 2000);
        EXPECT_EQ(device.telemetries.size(), 2);
    }
}

TEST(UUGreen, RetainsFrameAndMonitorFilters) {
    TestDevice device;
    device.set_module_address({1});
    auto packet = std::vector<uint8_t>(UU::Packet(UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 1000));
    device.rx_handler(UU::encode_can_id(1), packet);
    device.rx_handler(CAN_EFF_FLAG | (UU::encode_can_id(1) ^ (3U << 21)), packet);
    device.rx_handler(CAN_EFF_FLAG | UU::encode_can_id(1), {});
    EXPECT_TRUE(device.telemetries.empty());
    device.rx_handler(CAN_EFF_FLAG | (UU::encode_can_id(1) & ~(0xfU << 21)), packet);
    EXPECT_EQ(device.telemetries.size(), 1);
}

TEST(UUGreen, TransmitsOnlyToConfiguredAddresses) {
    for (const auto addresses : {std::vector<uint8_t>{1, 2}, std::vector<uint8_t>{0}}) {
        {
            std::lock_guard<std::mutex> lock(sent_mutex);
            sent_frames.clear();
        }
        {
            TestDevice device;
            // An unconfigured instance must not send, even with its polling thread running.
            {
                std::unique_lock<std::mutex> lock(sent_mutex);
                EXPECT_FALSE(
                    sent_changed.wait_for(lock, std::chrono::milliseconds(150), [] { return !sent_frames.empty(); }));
            }
            device.set_module_address(addresses);
            device.request_module_info();
            device.set_voltage_current(400, 20);
            device.switch_on(true);
            {
                std::unique_lock<std::mutex> lock(sent_mutex);
                ASSERT_TRUE(sent_changed.wait_for(lock, std::chrono::seconds(7), [&] {
                    for (auto address : addresses) {
                        for (auto command : {UU::CommandType::Vout, UU::CommandType::Iout_slow, UU::CommandType::ReadSN,
                                             UU::CommandType::ShutdownDCDC, UU::CommandType::VoutReference,
                                             UU::CommandType::IoutLimit, UU::CommandType::HiMode_LoMode_Selection}) {
                            bool found = false;
                            for (const auto& frame : sent_frames)
                                found |= frame.address == address && frame.command == command;
                            if (!found)
                                return false;
                        }
                    }
                    return true;
                }));
            }
        }
        std::lock_guard<std::mutex> lock(sent_mutex);
        for (const auto& frame : sent_frames)
            EXPECT_NE(std::find(addresses.begin(), addresses.end(), frame.address), addresses.end());
    }
}

TEST(UUGreen, LogsIgnoredAddressOnceWithInterfaceAndCanId) {
    using Sink = boost::log::sinks::synchronous_sink<boost::log::sinks::text_ostream_backend>;
    auto stream = boost::make_shared<std::ostringstream>();
    auto sink = boost::make_shared<Sink>();
    sink->locked_backend()->add_stream(stream);
    sink->set_formatter(boost::log::expressions::stream << boost::log::expressions::smessage);
    auto core = boost::log::core::get();
    core->add_sink(sink);
    {
        TestDevice device;
        device.set_module_address({1, 2}, "test-can");
        for (int i = 0; i < 3; ++i)
            device.receive(3, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 1000);
        device.receive(4, UU::MessageType::ReadDataResponse, UU::CommandType::Vout, 1000);
    }
    core->remove_sink(sink);
    const auto log = stream->str();
    const std::string message = "UUGreen [test-can]: ignoring unconfigured module 3, CAN ID: 0x";
    const auto first = log.find(message);
    ASSERT_NE(first, std::string::npos);
    EXPECT_EQ(log.find(message, first + message.size()), std::string::npos);
    EXPECT_NE(log.find("ignoring unconfigured module 4"), std::string::npos);
    EXPECT_EQ(log.find("RX packet"), std::string::npos);
}
