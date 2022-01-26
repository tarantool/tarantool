# Target OS: FreeBSD 12.2 (RELEASE) and FreeBSD 13.0 (RELEASE)

## 1. Install necessary packages:

```bash
pkg install git cmake gmake readline icu libiconv
```

## 2. Download & build the Tarantool source code:

```bash
git clone git://github.com/tarantool/tarantool.git

cd tarantool
mkdir build && cd build
git submodule update --init --recursive
cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
gmake
```

## 3. Set up Python 3.8

Install testing dependencies either from packages or with pip.

### 3.1. Installing from packages:

```bash
pkg install python38 py38-yaml py38-six py38-gevent
```

### 3.2. Using pip:

```bash
pkg install py38-virtualenv py38-pip
virtualenv .venv
source .venv/bin/activate
pip install -r ../test-run/requirements.txt
```

### 3.3. Fix Python path if necessary

```bash
which python3 || ln -s /usr/local/bin/python3.8 /usr/local/bin/python3
```

## 4. Run the Tarantool test suite

To run all tests, execute:

```bash
gmake test
```
