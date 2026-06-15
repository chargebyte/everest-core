// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <memory>
#include <optional>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/connection_abstract.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/session/iso.hpp>

#include "../fsm/helper.hpp"

namespace iso15118 {

namespace {

using Signal = session::feedback::Signal;

class FakeConnection : public io::IConnection {
public:
    void set_event_callback(const io::ConnectionEventCallback& callback_) final {
        callback = callback_;
    }

    io::Ipv6EndPoint get_public_endpoint() const final {
        return {15118, {0, 0, 0, 0, 0, 0, 0, 1}};
    }

    void write(const uint8_t*, size_t) final {
    }

    io::ReadResult read(uint8_t*, size_t) final {
        return {};
    }

    void close() final {
        ++close_calls;
        if (callback) {
            callback(io::ConnectionEvent::CLOSED);
        }
    }

    std::optional<io::sha512_hash_t> get_vehicle_cert_hash() const final {
        return std::nullopt;
    }

    void trigger(io::ConnectionEvent event) const {
        REQUIRE(callback);
        callback(event);
    }

    std::size_t close_calls{0};

private:
    io::ConnectionEventCallback callback{nullptr};
};

// The session code emits log and session-log callbacks during construction and shutdown.
// The test only cares about the shutdown behavior, so both callbacks are stubbed out here.
void install_test_loggers() {
    io::set_logging_callback([](LogLevel, std::string) {});
    session::logging::set_session_log_callback([](std::size_t, const session::logging::Event&) {});
}

} // namespace

SCENARIO("ISO15118 session shutdown handling") {
    // The session must stop immediately when the EVCC drops the transport connection.
    // If the EV charger initiates the stop, the normal close path should still emit one terminate signal.
    install_test_loggers();

    auto make_session = [](std::unique_ptr<FakeConnection>& connection, std::vector<Signal>& signals) {
        session::feedback::Callbacks callbacks;
        callbacks.signal = [&signals](Signal signal) { signals.push_back(signal); };

        std::optional<d20::PauseContext> pause_ctx;
        return Session(std::move(connection), d20::SessionConfig(create_default_evse_setup()), callbacks, pause_ctx);
    };

    // EVCC side: a hard transport close must terminate the session immediately.
    GIVEN("the EVCC closes the transport connection") {
        auto connection = std::make_unique<FakeConnection>();
        auto* fake_connection = connection.get();
        std::vector<Signal> signals;
        auto session = make_session(connection, signals);

        // The connection opens first, then the EVCC drops the transport.
        fake_connection->trigger(io::ConnectionEvent::ACCEPTED);
        fake_connection->trigger(io::ConnectionEvent::CLOSED);

        // The session is finished right away and only one termination signal is sent.
        THEN("the session ends immediately and only one shutdown signal is emitted") {
            REQUIRE(session.is_finished());
            REQUIRE(signals == std::vector<Signal>{Signal::DLINK_TERMINATE});

            // Polling after the peer disconnect must not trigger a charger-side close.
            session.poll();

            REQUIRE(fake_connection->close_calls == 0);
        }
    }

    // Charger side: a normal stop still closes the transport and emits one terminate signal.
    GIVEN("the EV charger closes the session") {
        auto connection = std::make_unique<FakeConnection>();
        auto* fake_connection = connection.get();
        std::vector<Signal> signals;
        auto session = make_session(connection, signals);

        // The charger is connected and then initiates the shutdown path itself.
        fake_connection->trigger(io::ConnectionEvent::ACCEPTED);
        session.close();

        // The session is finished and the transport close is counted once.
        THEN("the charger initiated shutdown emits a single terminate signal") {
            REQUIRE(session.is_finished());
            REQUIRE(fake_connection->close_calls == 1);
            REQUIRE(signals == std::vector<Signal>{Signal::DLINK_TERMINATE});
        }
    }
}

} // namespace iso15118
