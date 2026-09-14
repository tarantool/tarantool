#!/bin/bash
#
# This script generates SSL keys and certificates used for testing.

HOST=tarantool.io
NEWKEY_ARG=rsa:4096
DAYS_ARG=36500

#
# Generates new CA.
#
# The new private key and certificate are written to files "${ca}.key" and
# "${ca}.crt" respectively where "${ca}" is the name of the new CA as given
# in the first argument.
#
gen_ca()
{
    local ca="${1}"
    openssl req -new -nodes -newkey "${NEWKEY_ARG}" -days "${DAYS_ARG}" -x509 \
        -subj "/OU=Unknown/O=Unknown/L=Unknown/ST=unknown/C=AU" \
        -keyout "${ca}.key" -out "${ca}.crt"
}

#
# Generates new certificate and private key signed by the given CA.
#
# The new private key and certificate are written to files "${cert}.key" and
# "${cert}.crt" respectively where "${cert}" is the certificate name as given
# in the first argument. The CA and private key used for signing the new
# certificate should be located in "${ca}.cert" and "${ca}.key" where "${ca}"
# is the value of the second argument.
#
gen_cert()
{
    local cert="${1}"
    local ca="${2}"
    openssl req -new -nodes -newkey "${NEWKEY_ARG}" \
        -subj "/CN=${HOST}/OU=Unknown/O=Unknown/L=Unknown/ST=unknown/C=AU" \
        -keyout "${cert}.key" -out "${cert}.csr"
    openssl x509 -req -days "${DAYS_ARG}" \
        -CAcreateserial -CA "${ca}.crt" -CAkey "${ca}.key" \
        -in "${cert}.csr" -out "${cert}.crt"
    rm -f "${cert}.csr"
}

#
# Encrypt private key file.
# $1 - file name without extension
# $2 - pass phrase
#
# Encrypted key is written to ${1}.enc.key
#
encrypt_key()
{
    local key="${1}"
    local pass="${2}"
    openssl rsa -aes256 -passout "pass:${pass}" \
        -in "${key}.key" -out "${key}.enc.key"
}

gen_ca ca
gen_cert server ca
gen_cert server_2 ca
gen_cert server_3 ca
gen_cert client ca
gen_cert client_2 ca
encrypt_key server 1q2w3e
encrypt_key client 123qwe

gen_ca ca2
gen_cert server2 ca2
gen_cert server2_2 ca2
gen_cert server2_3 ca2
gen_cert client2 ca2
gen_cert client2_2 ca2
