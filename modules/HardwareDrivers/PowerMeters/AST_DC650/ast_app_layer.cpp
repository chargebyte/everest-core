// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ast_app_layer.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <nlohmann/json.hpp>

namespace ast_app_layer {

namespace {

void append(Bytes& bytes, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i) {
        bytes.push_back(value & 0xff);
        value >>= 8;
    }
}

void check_text(const std::string& text, std::size_t maximum, const char* field) {
    if (text.size() > maximum ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32 || c > 126; })) {
        throw std::invalid_argument(std::string(field) + ": expected printable ASCII, maximum " +
                                    std::to_string(maximum) + " bytes");
    }
}

UserIdType identification_type(types::powermeter::OCMFIdentificationType type) {
    using T = types::powermeter::OCMFIdentificationType;
    switch (type) {
    case T::NONE:
        return UserIdType::NONE;
    case T::DENIED:
        return UserIdType::DENIED;
    case T::UNDEFINED:
        return UserIdType::UNDEFINED;
    case T::ISO14443:
        return UserIdType::ISO14443;
    case T::ISO15693:
        return UserIdType::ISO15693;
    case T::EMAID:
        return UserIdType::EMAID;
    case T::EVCCID:
        return UserIdType::EVCCID;
    case T::EVCOID:
        return UserIdType::EVCOID;
    case T::ISO7812:
        return UserIdType::ISO7812;
    case T::CARD_TXN_NR:
        return UserIdType::CAR_TXN_NR;
    case T::CENTRAL:
        return UserIdType::CENTRAL;
    case T::CENTRAL_1:
        return UserIdType::CENTRAL_1;
    case T::CENTRAL_2:
        return UserIdType::CENTRAL_2;
    case T::LOCAL:
        return UserIdType::LOCAL;
    case T::LOCAL_1:
        return UserIdType::LOCAL_1;
    case T::LOCAL_2:
        return UserIdType::LOCAL_2;
    case T::PHONE_NUMBER:
        return UserIdType::PHONE_NUMBER;
    case T::KEY_CODE:
        return UserIdType::KEY_CODE;
    }
    throw std::invalid_argument("Unknown identification type");
}

} // namespace

std::uint64_t unsigned_le(const Bytes& data, std::size_t offset, std::size_t width) {
    if (width == 0 || width > 8 || offset > data.size() || width > data.size() - offset) {
        throw std::runtime_error("Invalid AST numeric field length");
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value |= static_cast<std::uint64_t>(data[offset + i]) << (8 * i);
    }
    return value;
}

std::int64_t signed_le(const Bytes& data, std::size_t offset, std::size_t width) {
    const auto value = unsigned_le(data, offset, width);
    const auto sign = std::uint64_t{1} << (width * 8 - 1);
    if ((value & sign) == 0) {
        return static_cast<std::int64_t>(value);
    }
    // Avoid implementation-defined unsigned-to-signed conversion, including INT64_MIN.
    const auto mask = width == 8 ? std::numeric_limits<std::uint64_t>::max() : (sign * 2 - 1);
    return -1 - static_cast<std::int64_t>((~value) & mask);
}

Bytes command(CommandType type, const Bytes& data) {
    if (data.size() > 4995) {
        throw std::invalid_argument("AST command exceeds maximum length");
    }
    Bytes bytes;
    append(bytes, static_cast<std::uint16_t>(type), 2);
    append(bytes, data.size() + 5, 2);
    bytes.push_back(0);
    bytes.insert(bytes.end(), data.begin(), data.end());
    return bytes;
}

std::vector<Response> parse_responses(const Bytes& frame, std::uint8_t address) {
    if (frame.empty() || frame.front() != address) {
        throw std::runtime_error("Unexpected AST response address");
    }
    std::vector<Response> responses;
    for (std::size_t offset = 1; offset < frame.size();) {
        if (frame.size() - offset < 5) {
            throw std::runtime_error("Truncated AST response header");
        }
        const auto length = unsigned_le(frame, offset + 2, 2);
        if (length < 5 || length > 5000 || length > frame.size() - offset || frame[offset + 4] > 9) {
            throw std::runtime_error("Invalid AST response length or status");
        }
        responses.push_back({static_cast<CommandType>(unsigned_le(frame, offset, 2)),
                             static_cast<CommandResult>(frame[offset + 4]),
                             Bytes(frame.begin() + offset + 5, frame.begin() + offset + length)});
        offset += length;
        if (responses.size() > 10) {
            throw std::runtime_error("Too many AST responses in one frame");
        }
    }
    if (responses.empty()) {
        throw std::runtime_error("Empty AST response");
    }
    return responses;
}

ProtocolVariant detect_variant(const Bytes& data) {
    if (data.size() == 14) {
        return ProtocolVariant::dc_meter_v4_9;
    }
    if (data.size() == 38) {
        return ProtocolVariant::detached_display_v6_5;
    }
    throw std::runtime_error(
        "Cannot detect AST protocol from charge point ID length; configure protocol_variant explicitly");
}

std::string string(const Bytes& data) {
    const auto end = std::find(data.begin(), data.end(), 0);
    if (std::any_of(end, data.end(), [](auto byte) { return byte != 0; })) {
        throw std::runtime_error("Unexpected data after AST string terminator");
    }
    return std::string(data.begin(), end);
}

std::string public_key(const Bytes& data) {
    const auto key = string(data);
    if (key.size() != 130 || key.substr(0, 2) != "04" || !std::all_of(key.begin(), key.end(), [](unsigned char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        })) {
        throw std::runtime_error("Expected uncompressed 65-byte AST public key in hexadecimal ASCII");
    }
    return key;
}

Bytes start_command(const types::powermeter::TransactionReq& request, ProtocolVariant variant, std::int8_t offset,
                    std::uint32_t unix_seconds) {
    const auto user = request.identification_data.value_or("");
    auto tariff = request.tariff_text.value_or("");
    check_text(user, 40, "Identification data");
    check_text(request.transaction_id, 36, "Transaction ID");
    if (request.transaction_id.empty() || request.transaction_id.find("<=>") != std::string::npos) {
        throw std::invalid_argument("Transaction ID is empty or contains reserved <=> marker");
    }
    if (tariff.find("#TT#") != std::string::npos || tariff.find("<=>") != std::string::npos) {
        throw std::invalid_argument("Tariff contains reserved marker");
    }
    if (variant == ProtocolVariant::dc_meter_v4_9) {
        tariff += "<=>" + request.transaction_id;
    }
    check_text(tariff, 250, "Tariff text");
    Bytes data;
    append(data, unix_seconds, 4);
    data.push_back(static_cast<std::uint8_t>(offset));
    data.push_back(request.identification_status == types::powermeter::OCMFUserIdentificationStatus::ASSIGNED ? 1 : 0);
    data.push_back(static_cast<std::uint8_t>(identification_type(request.identification_type)));
    data.insert(data.end(), user.begin(), user.end());
    data.resize(47, 0);
    const std::string marker = "#TT#";
    data.insert(data.end(), marker.begin(), marker.end());
    data.insert(data.end(), tariff.begin(), tariff.end());
    if (variant == ProtocolVariant::detached_display_v6_5) {
        data.push_back(0);
    }
    data.insert(data.end(), marker.begin(), marker.end());
    if (variant == ProtocolVariant::detached_display_v6_5) {
        data.insert(data.end(), request.transaction_id.begin(), request.transaction_id.end());
        data.push_back(0);
    }
    return command(CommandType::START_TRANSACTION, data);
}

OcmfRecord validate_ocmf(const Bytes& data, const std::string& transaction_id, bool associated_by_request) {
    const auto text = string(data);
    if (text.compare(0, 5, "OCMF|") != 0) {
        throw std::runtime_error("Missing OCMF header");
    }
    // Parse JSON boundaries rather than splitting at '|', which is legal inside JSON strings.
    std::size_t position = 5;
    const auto parse_object = [&]() {
        const auto begin = position;
        bool quoted = false, escaped = false;
        int depth = 0;
        for (; position < text.size(); ++position) {
            const auto c = text[position];
            if (quoted) {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    quoted = false;
            } else if (c == '"')
                quoted = true;
            else if (c == '{')
                ++depth;
            else if (c == '}' && --depth == 0) {
                ++position;
                return nlohmann::json::parse(text.substr(begin, position - begin));
            }
        }
        throw std::runtime_error("Truncated OCMF JSON");
    };
    const auto values = parse_object();
    if (position >= text.size() || text[position++] != '|') {
        throw std::runtime_error("Missing OCMF signature separator");
    }
    const auto signature = parse_object();
    const auto sd = signature.value("SD", std::string{});
    if (position != text.size() || !values.contains("RD") || !values["RD"].is_array() || values["RD"].empty() ||
        signature.value("SA", std::string{}).empty() || sd.empty() || sd.size() % 2 != 0 ||
        !std::all_of(sd.begin(), sd.end(), [](unsigned char c) { return std::isxdigit(c); })) {
        throw std::runtime_error("Incomplete OCMF readings or signature");
    }
    if (!associated_by_request) {
        const auto tariff = values.value("TT", std::string{});
        const auto suffix = "<=>" + transaction_id;
        if (transaction_id.empty() || tariff.size() < suffix.size() ||
            tariff.compare(tariff.size() - suffix.size(), suffix.size(), suffix) != 0) {
            throw std::runtime_error("OCMF transaction ID does not match requested transaction");
        }
    }
    return {text, signature.at("SA").get<std::string>()};
}

} // namespace ast_app_layer
