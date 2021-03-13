# Tarantool static build tooling

These files help to prepare environment for building Tarantool
statically. And builds it.

## Prerequisites

Please install following tools and libraries that will
be necessary for building and testing:
* git
* A C/C++ compiler.

  Ordinarily, this is gcc and g++ version 4.6 or later. On Mac OS X, this
  is Clang version 3.2+.
* cmake
* make
* Python and modules.

  Python interpreter is not necessary for building Tarantool itself, unless you
  intend to use the “Run the test suite". For all platforms, this is python
  version 3.x. You need the following Python modules:
  * pyyaml
  * gevent
  * six

### Here is an examples for your OS:

CentOS:

```bash
yum install -y \
    git perl gcc cmake make gcc-c++ libstdc++-static \
    python3-msgpack python3-yaml python3-six python3-gevent
```

Ubuntu/Debian:

```bash
apt-get install -y \
    build-essential cmake make coreutils sed \
    python3 python3-msgpack python3-yaml python3-six python3-gevent
```

MacOS:

Before you start please install default Xcode Tools by Apple:

```bash
sudo xcode-select --install
sudo xcode-select -switch /Applications/Xcode.app/Contents/Developer
```

Install brew using command from
[Homebrew repository instructions](https://github.com/Homebrew/inst)

After that run next script:

```bash
brew install cmake
pip install --user --force-reinstall -r test-run/requirements.txt
```

## Usage

```bash
cmake .
make -j
ctest -V
```

## Customize your build

If you want to customise build, you need to set `CMAKE_TARANTOOL_ARGS` variable

### Usage

There are three types of `CMAKE_BUILD_TYPE`:
* Debug - default
* Release
* RelWithDebInfo

And you want to build tarantool with RelWithDebInfo:

```bash
cmake -DCMAKE_TARANTOOL_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo" .
```
