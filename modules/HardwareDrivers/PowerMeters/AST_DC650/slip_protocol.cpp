// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "slip_protocol.hpp"

namespace slip_protocol {

std::vector<std::uint8_t> SlipProtocol::package_single(std::uint8_t address, const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> raw{address};
    raw.insert(raw.end(), payload.begin(), payload.end());
    const auto crc = calculate_xModem_crc16(raw);
    raw.push_back(crc & 0xff);
    raw.push_back(crc >> 8);
    std::vector<std::uint8_t> frame{0xc0};
    for (auto byte : raw) {
        if (byte == 0xc0 || byte == 0xdb) {
            frame.push_back(0xdb);
            frame.push_back(byte == 0xc0 ? 0xdc : 0xdd);
        } else {
            frame.push_back(byte);
        }
    }
    frame.push_back(0xc0);
    return frame;
}

std::vector<std::uint8_t> SlipProtocol::package_multi(std::uint8_t address,
                                                      const std::vector<std::vector<std::uint8_t>>& commands) {
    std::vector<std::uint8_t> payload;
    for (const auto& command : commands) {
        payload.insert(payload.end(), command.begin(), command.end());
    }
    return package_single(address, payload);
}

void SlipProtocol::reset() {
    pending.clear();
    message_queue.clear();
    in_frame = escaped = malformed = false;
}

SlipReturnStatus SlipProtocol::unpack(std::vector<std::uint8_t>& bytes, std::uint8_t address) {
    auto result = SlipReturnStatus::SLIP_OK;
    for (auto byte : bytes) {
        if (byte == 0xc0) {
            if (in_frame && (!pending.empty() || malformed || escaped)) {
                if (malformed || escaped || pending.size() < 3) {
                    result = SlipReturnStatus::SLIP_ERROR_MALFORMED;
                } else {
                    const auto crc = pending[pending.size() - 2] | (pending.back() << 8);
                    pending.resize(pending.size() - 2);
                    if (crc != calculate_xModem_crc16(pending)) {
                        result = SlipReturnStatus::SLIP_ERROR_CRC_MISMATCH;
                    } else if (pending.front() == address || address == 0xff) {
                        message_queue.push_back(pending);
                    }
                }
            }
            pending.clear();
            escaped = malformed = false;
            in_frame = true;
        } else if (in_frame && !malformed) {
            if (escaped) {
                if (byte != 0xdc && byte != 0xdd) {
                    malformed = true;
                } else {
                    pending.push_back(byte == 0xdc ? 0xc0 : 0xdb);
                }
                escaped = false;
            } else if (byte == 0xdb) {
                escaped = true;
            } else {
                pending.push_back(byte);
            }
            // One response contains at most ten commands, each at most 5000 bytes.
            if (pending.size() > 50003) {
                malformed = true;
                pending.clear();
            }
        }
    }
    return result;
}

std::vector<std::uint8_t> SlipProtocol::retrieve_single_message() {
    if (message_queue.empty()) {
        return {};
    }
    auto message = std::move(message_queue.front());
    message_queue.pop_front();
    return message;
}

} // namespace slip_protocol
