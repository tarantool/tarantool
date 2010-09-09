#!/bin/bash

set -e
shopt -s extglob
read -p "Enter revision: " ver

git clone . ../tarantool-$ver
rm -rf ../tarantool-$ver/.git
echo HAVE_GIT=0 > ../tarantool-$ver/config.mk
echo "const char tarantool_version_string[] = \"$ver\";" > ../tarantool-$ver/tarantool_version.h
(cd ..; tar zcvf tarantool-$ver.tar.gz tarantool-$ver)
git tag $ver
