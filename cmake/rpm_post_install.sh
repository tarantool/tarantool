#!/bin/sh

echo "creating user and group"
groupadd tarantool
useradd -r -g tarantool tarantool

echo "making single instance setup"
/usr/local/bin/tarantool_topology.sh -y 1
