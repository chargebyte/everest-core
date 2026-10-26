// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once
#include "ast_app_layer.hpp"
#include "serial_device.hpp"
#include "slip_protocol.hpp"

namespace ast_app_layer {

class Transport {
public:
    virtual ~Transport() = default;
    virtual std::vector<Response> exchange(const std::vector<Bytes>& commands) = 0;
};

class SerialTransport : public Transport {
public:
    SerialTransport(const std::string& port, int baudrate, int parity, std::uint8_t address, bool ignore_echo);
    std::vector<Response> exchange(const std::vector<Bytes>& commands) override;

private:
    serial_device::SerialDevice serial;
    slip_protocol::SlipProtocol slip;
    int baudrate;
    std::uint8_t address;
    bool ignore_echo;
};

} // namespace ast_app_layer
