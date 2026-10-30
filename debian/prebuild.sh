#!/bin/bash

set -exu

# Ubuntu Xenial has a CMake version that doesn't support
# keywords used in curl's CMake.
if [[ $DIST == "xenial" ]]; then
  wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
  echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ xenial main' | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
  sudo apt-get update
fi
