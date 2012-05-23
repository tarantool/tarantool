#!/bin/sh

echo "Creating a user and a group"
groupadd tarantool
useradd -r -g tarantool tarantool

echo "Performing a single instance setup"
/usr/local/bin/tarantool_deploy.sh --yes 1.1
