# Target OS: OpenBSD 6.6 amd64

## 1. Install necessary packages

```bash
pkg_add git m4 curl cmake gmake
```

## 2. Download & build the Tarantool source code

```bash
git clone git://github.com/tarantool/tarantool.git

cd tarantool
git submodule update --init --recursive
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_BUNDLED_LIBCURL=OFF ..
gmake -j
```

## 3. Install Python 3.x packages

Install testing dependencies either from packages or from pip.

### 3.1. From packages:

```bash
pkg_add python-3.6.7 py3-gevent py3-yaml
```

### 3.2. From pip:
```bash
pkg_add py-virtualenv
virtualenv .venv
source .venv/bin/activate
pip install -r ../test-run/requirements.txt
```

## 4. Run tarantool test suite

```bash
gmake test
```
