#!/bin/bash

set -exu

# Ubuntu Xenial has a CMake version that doesn't support
# keywords used in curl's CMake.
if [[ $DIST == "xenial" ]]; then
  wget -O - https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - | sudo tee /usr/share/keyrings/kitware-archive-keyring.gpg >/dev/null
  echo 'deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ xenial main' | sudo tee /etc/apt/sources.list.d/kitware.list >/dev/null
  sudo apt-get update
fi

# cdbs 0.4.177 removed its cmake class, which debian/rules includes, and
# Ubuntu Resolute has cdbs 0.4.182. Build with the cdbs of Ubuntu Noble.
if [[ $DIST == "resolute" ]]; then
  deb=cdbs_0.4.166_all.deb
  wget -O /tmp/$deb https://archive.ubuntu.com/ubuntu/pool/universe/c/cdbs/$deb
  echo "f0c178132e32a09329ed2290c45ddc22f30f600638a84c72a86b4c754de7c774  /tmp/$deb" | sha256sum -c
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --allow-downgrades /tmp/$deb
fi
