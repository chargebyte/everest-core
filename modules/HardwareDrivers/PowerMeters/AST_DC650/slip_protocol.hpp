// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/*
 * This is an implementation for the SLIP serial protocol
 */
#pragma once
#include <cstdint>
#include <deque>
#include <everest/crc/crc.hpp>
#include <vector>

namespace slip_protocol {

constexpr int SLIP_START_END_FRAME = 0xC0;
constexpr int SLIP_BROADCAST_ADDR = 0xFF;

constexpr std::uint16_t SLIP_SIZE_ON_ERROR = 1;

constexpr std::int8_t SLIP_ERROR_SIZE_ERROR = -1;
constexpr std::int8_t SLIP_ERROR_MALFORMED = -2;
constexpr std::int8_t SLIP_ERROR_CRC_MISMATCH = -3;

enum class SlipReturnStatus : std::int8_t
{
    SLIP_ERROR_CRC_MISMATCH = -3,
    SLIP_ERROR_MALFORMED = -2,
    SLIP_ERROR_SIZE_ERROR = -1,
    SLIP_OK = 0,
    SLIP_ERROR_UNINITIALIZED = 1
};

class SlipProtocol {

public:
    std::vector<std::uint8_t> package_single(std::uint8_t address, const std::vector<std::uint8_t>& payload);
    std::vector<std::uint8_t> package_multi(std::uint8_t address,
                                            const std::vector<std::vector<std::uint8_t>>& multi_payload);

    void reset();
    SlipReturnStatus unpack(std::vector<std::uint8_t>& message, std::uint8_t listen_to_address);
    auto get_message_counter() const {
        return message_queue.size();
    }
    std::vector<std::uint8_t> retrieve_single_message();

private:
    std::deque<std::vector<std::uint8_t>> message_queue;
    std::vector<std::uint8_t> pending;
    bool in_frame{false};
    bool escaped{false};
    bool malformed{false};
};

} // namespace slip_protocol
