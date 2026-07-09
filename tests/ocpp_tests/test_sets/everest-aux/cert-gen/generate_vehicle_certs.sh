#!/usr/bin/env bash
# Generate the vehicle client certificate chain into an existing certs tree.
# The chain is minted under an ephemeral V2G root that is discarded after
# signing; nothing verifies the vehicle chain against the committed everest-aux
# V2G root, so a self-consistent throwaway root is sufficient.
set -euo pipefail

usage() {
    echo "Usage: $0 <certs-directory>"
    exit 1
}

[ $# -eq 1 ] || usage

CERTS_DIR="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

EC_CURVE=prime256v1
SHA=-sha256
CIPHER=-aes-128-cbc
PASSWORD=123456

CA_VEHICLE="$CERTS_DIR/ca/vehicle"
CLIENT_VEHICLE="$CERTS_DIR/client/vehicle"
mkdir -p "$CA_VEHICLE" "$CLIENT_VEHICLE"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Ephemeral V2G root (signs VehicleSubCA1; never installed)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$TMP/V2G_ROOT_CA.key"
openssl req -new -key "$TMP/V2G_ROOT_CA.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/v2gRootCACert.cnf" -out "$TMP/V2G_ROOT_CA.csr"
openssl x509 -req -in "$TMP/V2G_ROOT_CA.csr" -extfile "$SCRIPT_DIR/v2gRootCACert.cnf" -extensions ext \
    -signkey "$TMP/V2G_ROOT_CA.key" -passin "pass:$PASSWORD" $SHA -set_serial 12345 \
    -out "$TMP/V2G_ROOT_CA.pem" -days 3650

# VehicleSubCA1 (signed by the V2G root)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$CLIENT_VEHICLE/VEHICLE_SUB_CA1.key"
openssl req -new -key "$CLIENT_VEHICLE/VEHICLE_SUB_CA1.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/vehicleSubCA1Cert.cnf" -out "$TMP/VEHICLE_SUB_CA1.csr"
openssl x509 -req -in "$TMP/VEHICLE_SUB_CA1.csr" -extfile "$SCRIPT_DIR/vehicleSubCA1Cert.cnf" -extensions ext \
    -CA "$TMP/V2G_ROOT_CA.pem" -CAkey "$TMP/V2G_ROOT_CA.key" -passin "pass:$PASSWORD" -set_serial 12360 \
    -out "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" -days 1460

# VehicleSubCA2 (signed by VehicleSubCA1)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$CLIENT_VEHICLE/VEHICLE_SUB_CA2.key"
openssl req -new -key "$CLIENT_VEHICLE/VEHICLE_SUB_CA2.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/vehicleSubCA2Cert.cnf" -out "$TMP/VEHICLE_SUB_CA2.csr"
openssl x509 -req -in "$TMP/VEHICLE_SUB_CA2.csr" -extfile "$SCRIPT_DIR/vehicleSubCA2Cert.cnf" -extensions ext \
    -CA "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" -CAkey "$CLIENT_VEHICLE/VEHICLE_SUB_CA1.key" -passin "pass:$PASSWORD" -set_serial 12361 \
    -out "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" -days 3650

# Vehicle leaf (signed by VehicleSubCA2)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$CLIENT_VEHICLE/VEHICLE_LEAF.key"
openssl req -new -key "$CLIENT_VEHICLE/VEHICLE_LEAF.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/vehicleLeafCert.cnf" -out "$TMP/VEHICLE_LEAF.csr"
openssl x509 -req -in "$TMP/VEHICLE_LEAF.csr" -extfile "$SCRIPT_DIR/vehicleLeafCert.cnf" -extensions ext \
    -CA "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" -CAkey "$CLIENT_VEHICLE/VEHICLE_SUB_CA2.key" -passin "pass:$PASSWORD" -set_serial 12362 \
    -out "$CLIENT_VEHICLE/VEHICLE_LEAF.pem" -days 1460

# Full chain, DER encodings and the key password file
cat "$CLIENT_VEHICLE/VEHICLE_LEAF.pem" "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" \
    > "$CLIENT_VEHICLE/VEHICLE_CERT_CHAIN.pem"
openssl x509 -in "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" -outform DER -out "$CA_VEHICLE/VEHICLE_SUB_CA1.der"
openssl x509 -in "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" -outform DER -out "$CA_VEHICLE/VEHICLE_SUB_CA2.der"
openssl x509 -in "$CLIENT_VEHICLE/VEHICLE_LEAF.pem" -outform DER -out "$CLIENT_VEHICLE/VEHICLE_LEAF.der"
echo "$PASSWORD" > "$CLIENT_VEHICLE/VEHICLE_LEAF_PASSWORD.txt"
