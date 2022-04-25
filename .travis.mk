#
# Travis CI rules
#

DOCKER_IMAGE?=packpack/packpack:debian-stretch
TEST_RUN_EXTRA_PARAMS?=
MAX_FILES?=65534
MAX_PROC?=2500
VARDIR?=/tmp/tnt
GIT_DESCRIBE=$(shell git describe HEAD)
COVERITY_BINS=/cov-analysis/bin

# Transform the ${PRESERVE_ENVVARS} comma separated variables list
# to the '-e key="value" -e key="value" <...>' string.
#
# Add PRESERVE_ENVVARS itself to the list to allow to use this
# make script again from the inner environment (if there will be
# a need).
comma := ,
ENVVARS := PRESERVE_ENVVARS $(subst $(comma), ,$(PRESERVE_ENVVARS))
DOCKER_ENV := $(foreach var,$(ENVVARS),-e $(var)="$($(var))")

all: package

package:
	git clone https://github.com/packpack/packpack.git packpack
	./packpack/packpack

test: test_$(TRAVIS_OS_NAME)

# Redirect some targets via docker
test_linux: docker_test_debian
coverage: docker_coverage_debian
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
		-e TRAVIS_JOB_ID=${TRAVIS_JOB_ID} \
		-e CMAKE_EXTRA_PARAMS=${CMAKE_EXTRA_PARAMS} \
		-e CC=${CC} \
		-e CXX=${CXX} \
		${DOCKER_ENV} \
		${DOCKER_IMAGE} \
		make -f .travis.mk $(subst docker_,,$@)

#########
# Linux #
#########

# Depends

# When dependencies in 'deps_debian' or 'deps_buster_clang_11' goal
# are changed, push a new docker image into GitLab Registry using
# the following command:
#
# $ make GITLAB_USER=foo -f .gitlab.mk docker_bootstrap
#
# It is highly recommended to only add dependencies (don't remove
# them), because all branches use the same latest image and it is
# often that a short-term branch is based on non-so-recent master
# commit, so the build requires old dependencies to be installed.
# See ce623a23416eb192ce70116fd14992e84e7ccbbe ('Enable GitLab CI
# testing') for more information.

deps_tests:
	pip install -r test-run/requirements.txt

deps_ubuntu_ghactions: deps_tests
	sudo apt-get update ${APT_EXTRA_FLAGS} && \
		sudo apt-get install -y -f libreadline-dev libunwind-dev \
		    autoconf automake libtool

deps_coverage_ubuntu_ghactions: deps_ubuntu_ghactions
	sudo apt-get install -y -f lcov
	sudo gem install coveralls-lcov
	# Link src/lib/uri/src to local src dircetory to avoid of issue:
	# /var/lib/gems/2.7.0/gems/coveralls-lcov-1.7.0/lib/coveralls/lcov/converter.rb:64:in
	#   `initialize': No such file or directory @ rb_sysopen -
	#   /home/runner/work/tarantool/tarantool/src/src/uri.c (Errno::ENOENT)
	ln -s ${PWD}/src src/src

deps_debian_packages:
	apt-get update ${APT_EXTRA_FLAGS} && apt-get install -y -f \
		build-essential cmake coreutils sed \
		libreadline-dev libncurses5-dev libyaml-dev libssl-dev \
		libcurl4-openssl-dev libunwind-dev libicu-dev \
		python python-pip python-setuptools python-dev \
		python-msgpack python-yaml python-argparse python-six python-gevent \
		python3 python3-gevent python3-six python3-yaml \
		lcov ruby clang llvm llvm-dev zlib1g-dev autoconf automake libtool

deps_debian: deps_debian_packages deps_tests

deps_buster_clang_8: deps_debian
	echo "deb http://apt.llvm.org/buster/ llvm-toolchain-buster-8 main" > /etc/apt/sources.list.d/clang_8.list
	echo "deb-src http://apt.llvm.org/buster/ llvm-toolchain-buster-8 main" >> /etc/apt/sources.list.d/clang_8.list
	wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
	apt-get update
	apt-get install -y clang-8 llvm-8-dev

deps_buster_clang_11: deps_debian
	echo "deb http://apt.llvm.org/buster/ llvm-toolchain-buster-11 main" > /etc/apt/sources.list.d/clang_11.list
	echo "deb-src http://apt.llvm.org/buster/ llvm-toolchain-buster-11 main" >> /etc/apt/sources.list.d/clang_11.list
	wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
	apt-get update
	apt-get install -y clang-11 llvm-11-dev

# Release

build_debian:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j $$(nproc)

test_debian_no_deps: build_debian
	make LuaJIT-test
	cd test && ./test-run.py --vardir ${VARDIR} --force $(TEST_RUN_EXTRA_PARAMS)

test_debian: deps_debian test_debian_no_deps

test_ubuntu_ghactions: deps_ubuntu_ghactions test_debian_no_deps

test_debian_clang11: deps_debian deps_buster_clang_11 test_debian_no_deps

# Debug

build_debug_debian:
	cmake . -DCMAKE_BUILD_TYPE=Debug
	make -j $$(nproc)

test_debug_debian_no_deps: build_debug_debian
	make LuaJIT-test
	cd test && ./test-run.py --vardir ${VARDIR} --force $(TEST_RUN_EXTRA_PARAMS)

debug_ubuntu_ghactions: deps_ubuntu_ghactions test_debug_debian_no_deps

# Coverage

build_coverage_debian:
	cmake . -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
	make -j $$(nproc)

test_coverage_debian_no_deps: build_coverage_debian
	make LuaJIT-test
	# Enable --long tests for coverage
	cd test && ./test-run.py --vardir ${VARDIR} --force $(TEST_RUN_EXTRA_PARAMS) --long
	lcov --compat-libtool --directory src/ --capture --output-file coverage.info.tmp \
		--rc lcov_branch_coverage=1 --rc lcov_function_coverage=1
	lcov --compat-libtool --remove coverage.info.tmp 'tests/*' 'third_party/*' '/usr/*' \
		--output-file coverage.info
	lcov --list coverage.info

coverage_debian: deps_debian test_coverage_debian_no_deps

coverage_ubuntu_ghactions: deps_coverage_ubuntu_ghactions test_coverage_debian_no_deps

# Coverity

build_coverity_debian: configure_debian
	export PATH=${PATH}:${COVERITY_BINS} ; \
		cov-build --dir cov-int make -j $$(nproc)

test_coverity_debian_no_deps: build_coverity_debian
	tar czvf tarantool.tgz cov-int
	@if [ -n "$(COVERITY_TOKEN)" ]; then \
		echo "Exporting code coverity information to scan.coverity.com"; \
		curl --form token=$(COVERITY_TOKEN) \
			--form email=tarantool@tarantool.org \
			--form file=@tarantool.tgz \
			--form version=${GIT_DESCRIBE} \
			--form description="Tarantool Coverity" \
			https://scan.coverity.com/builds?project=tarantool%2Ftarantool ; \
	fi;

deps_coverity_debian: deps_debian
	# check that coverity tools installed in known place
	@ls -al ${COVERITY_BINS} || \
		( echo "=================== ERROR: =====================" ; \
		  echo "Coverity binaries not found in: ${COVERITY_BINS}" ; \
		  echo "please install it there using instructions from:" ; \
		  echo "  https://scan.coverity.com/download?tab=cxx" ; \
		  echo ; \
		  exit 1 )

coverity_debian: deps_coverity_debian test_coverity_debian_no_deps

# ASAN

build_asan_debian:
	CC=clang-11 CXX=clang++-11 cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DENABLE_WERROR=ON -DENABLE_ASAN=ON ${CMAKE_EXTRA_PARAMS}
	make -j $$(nproc)

test_asan_debian_no_deps: build_asan_debian
	# FIXME: PUC-Rio-Lua-5.1 test suite is disabled for ASAN
	# due to https://github.com/tarantool/tarantool/issues/5880.
	# Run tests suites manually.
	ASAN=ON \
		LSAN_OPTIONS=suppressions=${PWD}/asan/lsan.supp \
		ASAN_OPTIONS=heap_profile=0:unmap_shadow_on_exit=1:detect_invalid_pointer_pairs=1:symbolize=1:detect_leaks=1:dump_instruction_bytes=1:print_suppressions=0 \
		make LuaJIT-tests lua-Harness-tests tarantool-tests
	# Temporary excluded some tests by issue #4360:
	#  - To exclude tests from ASAN checks the asan/asan.supp file
	#    was set at the build time in cmake/profile.cmake file.
	#  - To exclude tests from LSAN checks the asan/lsan.supp file
	#    was set in environment options to be used at run time.
	cd test && ASAN=ON \
		LSAN_OPTIONS=suppressions=${PWD}/asan/lsan.supp \
		ASAN_OPTIONS=heap_profile=0:unmap_shadow_on_exit=1:detect_invalid_pointer_pairs=1:symbolize=1:detect_leaks=1:dump_instruction_bytes=1:print_suppressions=0 \
		./test-run.py --vardir ${VARDIR} --force $(TEST_RUN_EXTRA_PARAMS)

test_asan_debian: deps_debian deps_buster_clang_11 test_asan_debian_no_deps

test_asan_ubuntu_ghactions: deps_ubuntu_ghactions test_asan_debian_no_deps

# Static build

deps_debian_static: deps_tests
	# Found that in Debian OS libunwind library built with dependencies to
	# liblzma library, but there is no liblzma static library installed,
	# while liblzma dynamic library exists. So the build dynamicaly has no
	# issues, while static build fails. To fix it we need to install
	# liblzma-dev package with static library only for static build.
	sudo apt-get install -y -f liblzma-dev

test_static_build: deps_debian_static
	CMAKE_EXTRA_PARAMS=-DBUILD_STATIC=ON make -f .travis.mk test_debian_no_deps

test_static_docker_build:
	docker build --no-cache --network=host -f Dockerfile.staticbuild -t static_build:tmp .
	docker run --rm -v ${PWD}/artifacts:/tarantool/test/var/artifacts static_build:tmp \
		-c "set -x && cd /tarantool/test && \
		        ./test-run.py --force box/admin || \
		        ( chmod -R a+rwx var/artifacts ; exit 1 )"

#######
# OSX #
#######

OSX_PKGS=openssl@1.1 readline curl icu4c libiconv zlib autoconf automake libtool \
	cmake python3

deps_osx:
	# install brew using command from Homebrew repository instructions:
	#   https://github.com/Homebrew/install
	# NOTE: 'echo' command below is required since brew installation
	# script obliges the one to enter a newline for confirming the
	# installation via Ruby script.
	brew update || echo | /usr/bin/ruby -e \
		"$$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
	# try to install the packages either upgrade it to avoid of fails
	# if the package already exists with the previous version
	brew install --force ${OSX_PKGS} || brew upgrade ${OSX_PKGS}
	pip3 install --force-reinstall -r test-run/requirements.txt

deps_osx_github_actions:
	# try to install the packages either upgrade it to avoid of fails
	# if the package already exists with the previous version
	brew install --force ${OSX_PKGS} || brew upgrade ${OSX_PKGS}
	pip3 install --force-reinstall -r test-run/requirements.txt

build_osx:
	# due swap disabling should be manualy configured need to
	# control it's status
	sysctl vm.swapusage
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j $$(sysctl -n hw.ncpu)

test_osx_no_deps: build_osx
	# Limits: Increase the maximum number of open file descriptors on macOS:
	#   Travis-ci needs the "ulimit -n <value>" call
	#   Gitlab-ci needs the "launchctl limit maxfiles <value>" call
	# Also gitlib-ci needs the password to change the limits, while
	# travis-ci runs under root user. Limit setup must be in the same
	# call as tests runs call.
	# Tests: Temporary excluded replication/ suite with some tests
	#        from other suites by issues #4357 and #4370
	sudo -S launchctl limit maxfiles ${MAX_FILES} || : ; \
		launchctl limit maxfiles || : ; \
		ulimit -n ${MAX_FILES} || : ; \
		ulimit -n ; \
		sudo -S launchctl limit maxproc ${MAX_PROC} || : ; \
		launchctl limit maxproc || : ; \
		ulimit -u ${MAX_PROC} || : ; \
		ulimit -u ; \
		rm -rf ${VARDIR} ; \
		make LuaJIT-test ; \
		cd test && ./test-run.py --vardir ${VARDIR} --force $(TEST_RUN_EXTRA_PARAMS)

test_osx: deps_osx test_osx_no_deps

test_osx_github_actions: deps_osx_github_actions test_osx_no_deps

###########
# FreeBSD #
###########

deps_freebsd:
	sudo pkg install -y git cmake gmake icu libiconv \
		python38 py38-yaml py38-six py38-gevent \
		autoconf automake libtool
	which python3 || sudo ln -s /usr/local/bin/python3.8 /usr/local/bin/python3

build_freebsd:
	if [ "$$(swapctl -l | wc -l)" != "1" ]; then sudo swapoff -a ; fi ; swapctl -l
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	gmake -j $$(sysctl -n hw.ncpu)

test_freebsd_no_deps: build_freebsd
	make LuaJIT-test
	cd test && ./test-run.py --vardir ${VARDIR} --force $(TEST_RUN_EXTRA_PARAMS)

test_freebsd: deps_freebsd test_freebsd_no_deps
