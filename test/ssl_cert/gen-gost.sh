#!/bin/bash
#
# This script generates a private SSL key and a self-signed certificate using
# the GOST algorithm.
#
# For the script to work, you need to install the GOST engine library:
#
# $ sudo apt install libengine-gost-openssl1.1

NAME=gost
DAYS_ARG=36500

OPENSSL_CONF="$(dirname $0)/openssl.cnf"

OPENSSL_CONF="${OPENSSL_CONF}" openssl genpkey -algorithm gost2001 \
    -pkeyopt paramset:A -out "${NAME}.key"

OPENSSL_CONF="${OPENSSL_CONF}" openssl req -new -x509 -days ${DAYS_ARG} \
    -subj '/C=RU' -key "${NAME}.key" -out "${NAME}.crt"
