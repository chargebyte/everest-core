// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "transport.hpp"
#include "diagnostics.hpp"
#include <algorithm>
#include <chrono>
#include <everest/logging.hpp>
#include <map>

namespace ast_app_layer {

SerialTransport::SerialTransport(const std::string& port, int baudrate, int parity, std::uint8_t address,
                                 bool ignore_echo) :
    baudrate(baudrate), address(address), ignore_echo(ignore_echo) {
    if (!serial.open_device(port, baudrate, ignore_echo, parity)) {
        throw std::runtime_error("Cannot configure AST serial port " + port);
    }
}

std::vector<Response> SerialTransport::exchange(const std::vector<Bytes>& commands) {
    if (commands.empty() || commands.size() > 10) {
        throw std::invalid_argument("Expected 1..10 AST commands");
    }
    std::map<CommandType, bool> expected;
    std::size_t maximum_reply = 3;
    for (const auto& request : commands) {
        const auto type = static_cast<CommandType>(unsigned_le(request, 0, 2));
        if (!expected.emplace(type, false).second) {
            throw std::invalid_argument("Duplicate command in AST request");
        }
        switch (type) {
        case CommandType::GET_OCMF:
        case CommandType::GET_LAST_OCMF:
        case CommandType::FIND_TRANSACTION_OCMF:
            maximum_reply += 5000;
            break;
        default:
            maximum_reply += 256;
            break;
        }
    }
    const auto request = slip.package_multi(address, commands);
    EVLOG_debug << "AST TX: " << module::conversions::hexdump(request);
    serial.discard_input();
    slip.reset();
    serial.tx(request);
    const auto begin = std::chrono::steady_clock::now();
    auto deadline = begin + std::chrono::milliseconds(2500);
    const auto wire_time = std::chrono::milliseconds((maximum_reply * 2 + request.size()) * 11000 / baudrate + 2500);
    bool received = false;
    std::size_t echo_position = 0;
    std::vector<Response> responses;
    while (std::chrono::steady_clock::now() < deadline) {
        Bytes bytes;
        bytes.reserve(256);
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        serial.rx(bytes, std::min<int>(100, std::max<std::int64_t>(1, remaining)), 1);
        if (bytes.empty())
            continue;
        if (!received) {
            received = true;
            deadline = begin + wire_time;
        }
        EVLOG_debug << "AST RX: " << module::conversions::hexdump(bytes);
        if (ignore_echo && echo_position < request.size()) {
            std::size_t consumed = 0;
            while (consumed < bytes.size() && echo_position < request.size()) {
                if (bytes[consumed++] != request[echo_position++]) {
                    throw std::runtime_error("AST serial echo differs from request");
                }
            }
            bytes.erase(bytes.begin(), bytes.begin() + consumed);
        }
        if (slip.unpack(bytes, address) != slip_protocol::SlipReturnStatus::SLIP_OK) {
            throw std::runtime_error("AST response has invalid SLIP framing or CRC");
        }
        while (slip.get_message_counter()) {
            for (auto response : parse_responses(slip.retrieve_single_message(), address)) {
                const auto match = expected.find(response.command);
                if (match == expected.end() || match->second) {
                    throw std::runtime_error("Unexpected or duplicate AST command response");
                }
                match->second = true;
                responses.push_back(std::move(response));
            }
        }
        if (responses.size() == expected.size())
            return responses;
    }
    throw std::runtime_error("AST response timeout or incomplete reply");
}

} // namespace ast_app_layer
