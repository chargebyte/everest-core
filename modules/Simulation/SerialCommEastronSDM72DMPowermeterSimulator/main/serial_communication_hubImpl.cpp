// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "serial_communication_hubImpl.hpp"

namespace module {
namespace main {

void serial_communication_hubImpl::init() {
}

void serial_communication_hubImpl::ready() {
}

types::serial_comm_hub_requests::Result
serial_communication_hubImpl::handle_modbus_read_holding_registers(int& target_device_id, int& first_register_address,
                                                                   int& num_registers_to_read) {
    // unused by this device
    return {};
}

types::serial_comm_hub_requests::Result
serial_communication_hubImpl::handle_modbus_read_input_registers(int& target_device_id, int& first_register_address,
                                                                 int& num_registers_to_read) {
    // your code for cmd modbus_read_input_registers goes here
    types::serial_comm_hub_requests::Result result;
    result.status_code = types::serial_comm_hub_requests::StatusCodeEnum::Error;

    if (num_registers_to_read != 2) {
        EVLOG_error << "Handling of anything but double registers is not implemented.";
        return result;
    }

    if (not this->mod->delayed_ready) {
        EVLOG_warning << "Behaving as not ready for register " << first_register_address;
        // EVLOG_warning << "Behaving as short read for register " << first_register_address;
        // result.value = {0};
        // result.status_code = types::serial_comm_hub_requests::StatusCodeEnum::Success;
        return result;
    }

    switch (first_register_address) {
    case 0:
        result.value = {17255, 45023};
        result.status_code = types::serial_comm_hub_requests::StatusCodeEnum::Success;
        break;
    case 2:
    case 4:
    case 6:
    case 8:
    case 10:
    case 12:
    case 14:
    case 16:
    case 52:
    case 74:
        // zero values
        result.value = {0, 0};
        result.status_code = types::serial_comm_hub_requests::StatusCodeEnum::Success;
        break;
    case 70:
        // frequency_Hz
        result.value = {16967, 52429};
        result.status_code = types::serial_comm_hub_requests::StatusCodeEnum::Success;
        break;
    case 72:
        // energy_Wh_import
        result.value = {16612, 46137};
        result.status_code = types::serial_comm_hub_requests::StatusCodeEnum::Success;
        break;
    default:
        break;
    }

    EVLOG_debug << "Supplying a response for register " << first_register_address << " with status code "
                << result.status_code;

    return result;
}

types::serial_comm_hub_requests::StatusCodeEnum serial_communication_hubImpl::handle_modbus_write_multiple_registers(
    int& target_device_id, int& first_register_address, types::serial_comm_hub_requests::VectorUint16& data_raw) {
    // unused by this device
    return types::serial_comm_hub_requests::StatusCodeEnum::Error;
}

types::serial_comm_hub_requests::StatusCodeEnum
serial_communication_hubImpl::handle_modbus_write_single_register(int& target_device_id, int& register_address,
                                                                  int& data) {
    // unused by this device
    return types::serial_comm_hub_requests::StatusCodeEnum::Error;
}

void serial_communication_hubImpl::handle_nonstd_write(int& target_device_id, int& first_register_address,
                                                       int& num_registers_to_read) {
    // unused by this device
}

types::serial_comm_hub_requests::Result serial_communication_hubImpl::handle_nonstd_read(int& target_device_id,
                                                                                         int& first_register_address,
                                                                                         int& num_registers_to_read) {
    // unused by this device
    return {};
}

} // namespace main
} // namespace module
