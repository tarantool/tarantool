#
# Travis CI rules
#

DOCKER_IMAGE?=packpack/packpack:debian-stretch
TEST_RUN_EXTRA_PARAMS?=
MAX_FILES?=65534

all: package

package:
	git clone https://github.com/packpack/packpack.git packpack
	./packpack/packpack

test: test_$(TRAVIS_OS_NAME)

# Redirect some targets via docker
test_linux: docker_test_debian
coverage: docker_coverage_debian
lto: docker_test_debian
lto_clang8: docker_test_debian_clang8
asan: docker_test_asan_debian

docker_%:
	mkdir -p ~/.cache/ccache
	docker run \
		--rm=true --tty=true \
		--volume "${PWD}:/tarantool" \
		--volume "${HOME}/.cache:/cache" \
		--workdir /tarantool \
		-e XDG_CACHE_HOME=/cache \
		-e CCACHE_DIR=/cache/ccache \
		-e COVERALLS_TOKEN=${COVERALLS_TOKEN} \
		-e TRAVIS_JOB_ID=${TRAVIS_JOB_ID} \
		-e CMAKE_EXTRA_PARAMS=${CMAKE_EXTRA_PARAMS} \
		-e APT_EXTRA_FLAGS="${APT_EXTRA_FLAGS}" \
		-e CC=${CC} \
		-e CXX=${CXX} \
		${DOCKER_IMAGE} \
		make -f .travis.mk $(subst docker_,,$@)

#########
# Linux #
#########

# Depends

deps_debian:
	apt-get update ${APT_EXTRA_FLAGS} && apt-get install -y -f \
		build-essential cmake coreutils sed \
		libreadline-dev libncurses5-dev libyaml-dev libssl-dev \
		libcurl4-openssl-dev libunwind-dev libicu-dev \
		python python-pip python-setuptools python-dev \
		python-msgpack python-yaml python-argparse python-six python-gevent \
		lcov ruby clang llvm llvm-dev

deps_buster_clang_8: deps_debian
	echo "deb http://apt.llvm.org/buster/ llvm-toolchain-buster-8 main" > /etc/apt/sources.list.d/clang_8.list
	echo "deb-src http://apt.llvm.org/buster/ llvm-toolchain-buster-8 main" >> /etc/apt/sources.list.d/clang_8.list
	wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
	apt-get update
	apt-get install -y clang-8 llvm-8-dev

# Release

build_debian:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j

test_debian_no_deps: build_debian
	cd test && /usr/bin/python test-run.py --force $(TEST_RUN_EXTRA_PARAMS)

test_debian: deps_debian test_debian_no_deps

test_debian_clang8: deps_debian deps_buster_clang_8 test_debian_no_deps

# Debug with coverage

build_coverage_debian:
	cmake . -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
	make -j

test_coverage_debian_no_deps: build_coverage_debian
	# Enable --long tests for coverage
	cd test && /usr/bin/python test-run.py --force $(TEST_RUN_EXTRA_PARAMS) --long
	lcov --compat-libtool --directory src/ --capture --output-file coverage.info.tmp
	lcov --compat-libtool --remove coverage.info.tmp 'tests/*' 'third_party/*' '/usr/*' \
		--output-file coverage.info
	lcov --list coverage.info
	@if [ -n "$(COVERALLS_TOKEN)" ]; then \
		echo "Exporting code coverage information to coveralls.io"; \
		gem install coveralls-lcov; \
		echo coveralls-lcov --service-name travis-ci --service-job-id $(TRAVIS_JOB_ID) --repo-token [FILTERED] coverage.info; \
		coveralls-lcov --service-name travis-ci --service-job-id $(TRAVIS_JOB_ID) --repo-token $(COVERALLS_TOKEN) coverage.info; \
	fi;

coverage_debian: deps_debian test_coverage_debian_no_deps

# ASAN

build_asan_debian:
	CC=clang-8 CXX=clang++-8 cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	CC=clang-8 CXX=clang++-8 cmake . -DENABLE_ASAN=ON ${CMAKE_EXTRA_PARAMS}
	make -j

test_asan_debian_no_deps: build_asan_debian
	# temporary excluded engine/ and replication/ suites with some tests from other suites by issue #4360
	cd test && ASAN=ON ASAN_OPTIONS=detect_leaks=0 ./test-run.py --force $(TEST_RUN_EXTRA_PARAMS) \
		app/ app-tap/ box/ box-py/ box-tap/ engine_long/ long_run-py/ luajit-tap/ \
		replication-py/ small/ sql/ sql-tap/ swim/ unit/ vinyl/ wal_off/ xlog/ xlog-py/

test_asan_debian: deps_debian deps_buster_clang_8 test_asan_debian_no_deps

#######
# OSX #
#######

deps_osx:
	brew update
	brew install openssl readline curl icu4c libiconv --force
	python2 -V || brew install python2 --force
	curl --silent --show-error --retry 5 https://bootstrap.pypa.io/get-pip.py >get-pip.py
	python get-pip.py --user
	pip install --user --force-reinstall -r test-run/requirements.txt

build_osx:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j

test_osx_no_deps: build_osx
	# Increase the maximum number of open file descriptors on macOS
	echo tarantool | sudo -S echo "Initialize sudo for the next commands..." :
	sudo sysctl -w kern.maxfiles=${MAX_FILES} || echo tarantool | sudo -S sysctl -w kern.maxfiles=${MAX_FILES} || :
	sysctl -w kern.maxfiles || :
	sudo sysctl -w kern.maxfilesperproc=${MAX_FILES} || echo tarantool | sudo -S sysctl -w kern.maxfilesperproc=${MAX_FILES} || :
	sysctl -w kern.maxfilesperproc || :
	sudo launchctl limit maxfiles ${MAX_FILES} || echo tarantool | sudo -S launchctl limit maxfiles ${MAX_FILES} || :
	launchctl limit maxfiles || :
	sudo ulimit -S -n ${MAX_FILES} || echo tarantool | sudo -S ulimit -S -n ${MAX_FILES} || :
	ulimit -S -n || :
	sudo ulimit -H -n ${MAX_FILES} || echo tarantool | sudo -S ulimit -H -n ${MAX_FILES} || :
	ulimit -H -n
	# temporary excluded replication/ suite with some tests from other suites by issues #4357 and #4370
	cd test && ./test-run.py --force $(TEST_RUN_EXTRA_PARAMS) \
		app/ app-tap/ box/ box-py/ box-tap/ engine/ engine_long/ long_run-py/ luajit-tap/ \
		replication-py/ small/ sql/ sql-tap/ swim/ unit/ vinyl/ wal_off/ xlog/ xlog-py/

test_osx: deps_osx test_osx_no_deps

###########
# FreeBSD #
###########

deps_freebsd:
	sudo pkg install -y git cmake gmake gcc coreutils \
		readline ncurses libyaml openssl curl libunwind icu \
		python27 py27-pip py27-setuptools py27-daemon \
		py27-yaml py27-argparse py27-six py27-gevent \
		gdb bash

build_freebsd:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	gmake -j

test_freebsd_no_deps: build_freebsd
	cd test && /usr/bin/python test-run.py --force $(TEST_RUN_EXTRA_PARAMS)

test_freebsd: deps_freebsd test_freebsd_no_deps

####################
# Sources tarballs #
####################

source:
	git clone https://github.com/packpack/packpack.git packpack
	TARBALL_COMPRESSOR=gz packpack/packpack tarball

# Push alpha and beta versions to <major>x bucket (say, 2x),
# stable to <major>.<minor> bucket (say, 2.2).
ifeq ($(TRAVIS_BRANCH),master)
GIT_DESCRIBE=$(shell git describe HEAD)
MAJOR_VERSION=$(word 1,$(subst ., ,$(GIT_DESCRIBE)))
MINOR_VERSION=$(word 2,$(subst ., ,$(GIT_DESCRIBE)))
else
MAJOR_VERSION=$(word 1,$(subst ., ,$(TRAVIS_BRANCH)))
MINOR_VERSION=$(word 2,$(subst ., ,$(TRAVIS_BRANCH)))
endif
BUCKET=tarantool.$(MAJOR_VERSION).$(MINOR_VERSION).src
ifeq ($(MINOR_VERSION),0)
BUCKET=tarantool.$(MAJOR_VERSION)x.src
endif
ifeq ($(MINOR_VERSION),1)
BUCKET=tarantool.$(MAJOR_VERSION)x.src
endif

source_deploy:
	pip install awscli --user
	aws --endpoint-url "${AWS_S3_ENDPOINT_URL}" s3 \
		cp build/*.tar.gz "s3://${BUCKET}/" \
		--acl public-read
