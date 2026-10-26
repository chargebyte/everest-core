// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "serial_device.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

namespace serial_device {

namespace {

std::runtime_error serial_error(const char* operation) {
    return std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

} // namespace

SerialDevice::~SerialDevice() {
    if (fd >= 0) {
        close(fd);
    }
}

bool SerialDevice::open_device(const std::string& device, int baud, bool /*ignore_echo*/, int parity) {
    fd = open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        return false;
    }
    speed_t speed;
    switch (baud) {
    case 9600:
        speed = B9600;
        break;
    case 19200:
        speed = B19200;
        break;
    case 38400:
        speed = B38400;
        break;
    case 57600:
        speed = B57600;
        break;
    case 115200:
        speed = B115200;
        break;
    case 230400:
        speed = B230400;
        break;
    default:
        return false;
    }
    termios tty{};
    if (tcgetattr(fd, &tty) != 0) {
        return false;
    }
    cfmakeraw(&tty);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    tty.c_cflag &= ~(CSIZE | PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;
    if (parity != 0) {
        tty.c_cflag |= PARENB;
        if (parity == 1) {
            tty.c_cflag |= PARODD;
        }
    }
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &tty) == 0;
}

void SerialDevice::discard_input() {
    std::scoped_lock lock(serial_mutex);
    if (tcflush(fd, TCIFLUSH) != 0) {
        throw serial_error("tcflush");
    }
}

int SerialDevice::rx(std::vector<std::uint8_t>& output, std::optional<int> initial_timeout_ms,
                     std::optional<int> in_msg_timeout_ms) {
    std::scoped_lock lock(serial_mutex);
    const int initial = initial_timeout_ms.value_or(SERIAL_RX_INITIAL_TIMEOUT_MS);
    const int inter_byte = in_msg_timeout_ms.value_or(SERIAL_RX_WITHIN_MESSAGE_TIMEOUT_MS);
    output.clear();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(initial);
    while (output.size() < output.capacity()) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            break;
        }
        timeval timeout{static_cast<time_t>(remaining.count() / 1000000),
                        static_cast<suseconds_t>(remaining.count() % 1000000)};
        fd_set descriptors;
        FD_ZERO(&descriptors);
        FD_SET(fd, &descriptors);
        const auto ready = select(fd + 1, &descriptors, nullptr, nullptr, &timeout);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw serial_error("select");
        }
        if (ready == 0) {
            break;
        }
        const auto previous = output.size();
        output.resize(output.capacity());
        const auto count = read(fd, output.data() + previous, output.size() - previous);
        output.resize(previous + (count > 0 ? count : 0));
        if (count < 0 && errno != EINTR && errno != EAGAIN) {
            throw serial_error("read");
        }
        if (count > 0) {
            deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(inter_byte);
        }
    }
    return output.size();
}

void SerialDevice::tx(const std::vector<std::uint8_t>& request) {
    std::scoped_lock lock(serial_mutex);
    std::size_t sent = 0;
    while (sent < request.size()) {
        const auto count = write(fd, request.data() + sent, request.size() - sent);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            throw serial_error("write");
        }
        sent += count;
    }
    if (tcdrain(fd) != 0) {
        throw serial_error("tcdrain");
    }
    // Echo is removed by the exchange layer, without discarding a following reply.
}

} // namespace serial_device
