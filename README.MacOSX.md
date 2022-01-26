# Target OS: macOS X 

In the Homebrew environment, you can download the latest `tarantool` package
with a single command:

```bash
brew install tarantool
```

This downloads an already built release version of Tarantool.

If you want to manually build Tarantool from sources, read the following
instructions on how to do it using either the default Apple developer software
(Xcode Tools), or external package managers (Homebrew or MacPorts).

## 1. Install necessary packages

For the default Xcode Tools by Apple:

```bash
sudo xcode-select --install
sudo xcode-select -switch /Applications/Xcode.app/Contents/Developer
```

For Homebrew:
```bash
brew install openssl readline curl icu4c libiconv zlib cmake python@3.8
```

For MacPorts:
```bash
port install binutils cmake ncurses zlib readline openssl python38
```

## 2. Install Python modules

Running tests requires Python 3.8 and a few modules, listed in 
[test-run/requirements.txt](./test-run/requirements.txt).
To install these packages, we recommend using `pip`, `easy_install`, or `setup.py`.

### 2.1 Using pip and a virtual environment

This is the recommended option.

First, install `virtualenv` package with Homebrew or MacPorts:

```bash
brew install pyenv-virtualenv
# or
port install py38-virtualenv
```

Next, initialize a virtual environment and install the requirements:

```bash
virtualenv .venv
source .venv/bin/activate
pip install -r test-run/requirements.txt
```

### 2.2 Using easy_install

```bash
sudo easy_install pyyaml
sudo easy_install argparse
sudo easy_install gevent
sudo easy_install six
```

### 2.3 Using setup.py

```bash
tar -xzf module.tar.gz
cd module-dir
sudo python setup.py install
```

Here `module` is the name of the installed python module and `module-dir`
is the name of the directory where the module's archive is deflated into.

## 3. Download & build the Tarantool source code

Download Tarantool source code from the repository at GitHub:

```bash
git clone https://github.com/tarantool/tarantool.git --recursive
git submodule update --init
```

Create a build directory and build the tarantool project manually, for example:

```bash
cd tarantool
mkdir build && cd build
cmake .. \
-DCMAKE_BUILD_TYPE=RelWithDebInfo \
-DCURL_INCLUDE_DIR=$(xcode-select --sdk macosx --show-sdk-path)/usr/include \
-DCURL_LIBRARY=/usr/lib/libcurl.dylib \
-DDARWIN_BUILD_TYPE=Ports
make
```

In this example, we are making a developer's build (`-DCMAKE_BUILD_TYPE=RelWithDebInfo`)
using Xcode Tools (`DCURL_INCLUDE_DIR=$(...)`) and MacPorts (`-DDARWIN_BUILD_TYPE=Ports`).
Note that release builds are not supported for macOS.

Remember also to set up the cmake's flag `-DDARWIN_BUILD_TYPE` depending on the package
manager you use: `-DDARWIN_BUILD_TYPE=None` for Xcode Tools and Homebrew, or
`-DDARWIN_BUILD_TYPE=Ports` for MacPorts. It is set to `None` by default.

Some Homebrew formulas are "keg-only", which means that they're not
symlinked into `/usr/local`. So, if you have used Homebrew for
dependencies, you would need following flags for it to find `openssl`
and GNU `readline`:

```bash
-DOPENSSL_ROOT_DIR=$(brew --prefix openssl) -DREADLINE_ROOT=$(brew --prefix readline)
```

## 4. Run the Tarantool test suite

To run all tests, execute:

```bash
make test
```
