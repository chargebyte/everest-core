// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef METHODS_CHARGEPOINT_HPP
#define METHODS_CHARGEPOINT_HPP

#include <string>

#include "../../data/DataStore.hpp"
#include "../RequestHandlerInterface.hpp"

using namespace data;

namespace RPCDataTypes = types::json_rpc_api;

namespace methods {

static const std::string METHOD_CHARGEPOINT_GET_EVSE_INFOS = "ChargePoint.GetEVSEInfos";
static const std::string METHOD_CHARGEPOINT_GET_ACTIVE_ERRORS = "ChargePoint.GetActiveErrors";
static const std::string METHOD_CHARGEPOINT_SET_HMI_INDICATION = "ChargePoint.SetHmiIndication";

/// This class includes all methods of the ChargePoint namespace.
/// It contains the data object and the methods to access it.
class ChargePoint {
public:
    // Constructor and Destructor
    ChargePoint() = delete;
    ChargePoint(DataStoreCharger& dataobj, std::shared_ptr<request_interface::RequestHandlerInterface> handler) :
        m_dataobj(dataobj), m_request_handler(std::move(handler)){};

    ~ChargePoint() = default;

    // Methods
    RPCDataTypes::ChargePointGetEVSEInfosResObj getEVSEInfos();
    RPCDataTypes::ChargePointGetActiveErrorsResObj getActiveErrors();
    RPCDataTypes::ErrorResObj set_hmi_indication(const std::string& indication);

private:
    DataStoreCharger& m_dataobj;
    std::shared_ptr<request_interface::RequestHandlerInterface> m_request_handler;
};

} // namespace methods

#endif // METHODS_CHARGEPOINT_HPP
