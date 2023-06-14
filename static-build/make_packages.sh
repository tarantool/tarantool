#!/bin/sh -x

# This script makes DEB and RPM packages with a statically compiled Tarantool
# binary inside. The build is performed in a Docker container, using PackPack
# docker image (centos-7) and CPack. The packpack/packpack:centos-7 image has
# all the necessary dependencies for building Tarantool and quite old glibc 2.17
# which theoretically provides compatibility of the created packages with any
# distro where glibc >= 2.17.
# Usually the script should be run by a CI/CD system, but it is possible to do
# this manually set the following variables:
#
#   CMAKE_TARANTOOL_ARGS - cmake args for the build
#   VERSION - package version
#   OUTPUT_DIR - output directory where to save packages

# Set default values for variables for successful run on the local machine.
CMAKE_TARANTOOL_ARGS="${CMAKE_TARANTOOL_ARGS:--DLUAJIT_ENABLE_GC64=ON;-DCMAKE_BUILD_TYPE=RelWithDebInfo}"
VERSION="${VERSION:-0.0.1}"
OUTPUT_DIR="${OUTPUT_DIR:-build}"

USER_ID=$(id -u)

echo "User ID: ${USER_ID}"
echo "CMake args: ${CMAKE_TARANTOOL_ARGS}"
echo "Package version: ${VERSION}"
echo "Output dir: ${OUTPUT_DIR}"

# Run building in a Docker container with the proper user to get artifacts
# with the correct permissions. If USER_ID is 0, then run as root. If USER_ID
# is not 0, then create the 'tarantool' user with the same user's ID as on the
# host machine. That helps avoid problems with permissions of artifacts.
if [ "${USER_ID}" = "0" ]; then
    docker run --rm --pull=always \
        --env VERSION=${VERSION} \
        --env OUTPUT_DIR=${OUTPUT_DIR} \
        --volume $(pwd):/tarantool \
        --workdir /tarantool/static-build/ \
        packpack/packpack:centos-7 sh -c "
            cmake3 -DCMAKE_TARANTOOL_ARGS=\"${CMAKE_TARANTOOL_ARGS}\" &&
            make -j $(nproc) &&
            make package"
else
    docker run --rm --pull=always \
        --env VERSION=${VERSION} \
        --env OUTPUT_DIR=${OUTPUT_DIR} \
        --volume $(pwd):/tarantool \
        --workdir /tarantool/static-build/ \
        packpack/packpack:centos-7 sh -c "
            useradd -u ${USER_ID} tarantool;
            su -m tarantool sh -c '
                cmake3 -DCMAKE_TARANTOOL_ARGS=\"${CMAKE_TARANTOOL_ARGS}\" &&
                make -j $(nproc) &&
                make package'"
fi
