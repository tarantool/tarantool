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
# Generates new intermediate CA signed by the given CA.
#
# The new private key and certificate are written to files "${ca}.key" and
# "${ca}.crt" respectively where "${ca}" is the name of the new CA as given
# in the first argument. The CA and private key used for signing the new
# certificate should be located in "${parent}.crt" and "${parent}.key" where
# "${parent}" is the value of the second argument.
#
gen_intermediate_ca()
{
    local ca="${1}"
    local parent="${2}"
    openssl req -new -nodes -newkey "${NEWKEY_ARG}" -subj \
        "/CN=intermediate/OU=Unknown/O=Unknown/L=Unknown/ST=unknown/C=AU" \
        -keyout "${ca}.key" -out "${ca}.csr"
    printf '%s\n' "basicConstraints=critical,CA:TRUE" \
        "keyUsage=critical,keyCertSign,cRLSign" > "${ca}.ext"
    openssl x509 -req -days "${DAYS_ARG}" -extfile "${ca}.ext" \
        -CAcreateserial -CA "${parent}.crt" -CAkey "${parent}.key" \
        -in "${ca}.csr" -out "${ca}.crt"
    rm -f "${ca}.csr" "${ca}.ext"
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

# The certificates of server3 and client3 are signed by an intermediate CA.
# The *_chain.crt files hold the certificate followed by the intermediate,
# and ca3_and_intermediate3.crt trusts both CAs.
gen_ca ca3
gen_intermediate_ca intermediate3 ca3
gen_cert server3 intermediate3
gen_cert client3 intermediate3
cat server3.crt intermediate3.crt > server3_chain.crt
cat client3.crt intermediate3.crt > client3_chain.crt
cat ca3.crt intermediate3.crt > ca3_and_intermediate3.crt
