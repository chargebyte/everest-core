// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MAIN_POWERMETER_IMPL_HPP
#define MAIN_POWERMETER_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 4
//

#include <generated/interfaces/powermeter/Implementation.hpp>

#include "../AST_DC650.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include "meter.hpp"
#include <atomic>
#include <condition_variable>
#include <thread>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace main {

struct Conf {
    std::string protocol_variant;
    int powermeter_device_id;
    std::string serial_port;
    int baudrate;
    int parity;
    int rs485_direction_gpio;
    bool ignore_echo;
    int gmt_offset_quarter_hours;
    bool publish_device_data;
    bool publish_device_diagnostics;
};

class powermeterImpl : public powermeterImplBase {
public:
    powermeterImpl() = delete;
    powermeterImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<AST_DC650>& mod, Conf& config) :
        powermeterImplBase(ev, "main"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    ~powermeterImpl();
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // command handler functions (virtual)
    virtual types::powermeter::TransactionStartResponse
    handle_start_transaction(types::powermeter::TransactionReq& value) override;
    virtual types::powermeter::TransactionStopResponse handle_stop_transaction(std::string& transaction_id) override;

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    void publish_measurement(const types::powermeter::Powermeter& values);
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<AST_DC650>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    std::unique_ptr<ast_app_layer::Meter> meter;
    std::thread worker;
    std::mutex shutdown_mutex;
    std::condition_variable shutdown_condition;
    bool shutting_down{false};
    std::string last_key;
    std::string last_poll_error;
    std::string last_diagnostic_error;
    void publish_key();
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace main
} // namespace module

#endif // MAIN_POWERMETER_IMPL_HPP
