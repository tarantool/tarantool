#
# Travis CI rules
#

DOCKER_IMAGE?=packpack/packpack:debian-stretch
DOCKER_IMAGE_TARANTOOL="registry.gitlab.com/tarantool/tarantool/testing/debian-stretch:latest"
TEST_RUN_EXTRA_PARAMS?=
MAX_FILES?=65534
MAX_PROC?=2500
OOS_SRC_PATH?=/source
OOS_BUILD_PATH?=/rw_bins
OOS_BUILD_RULE?=test_oos_no_deps
BIN_DIR=/usr/local/bin
OSX_VARDIR?=/tmp/tnt

CLOJURE_URL="https://download.clojure.org/install/linux-install-1.10.1.561.sh"
LEIN_URL="https://raw.githubusercontent.com/technomancy/leiningen/stable/bin/lein"
TERRAFORM_NAME="terraform_0.13.1_linux_amd64.zip"
TERRAFORM_URL="https://releases.hashicorp.com/terraform/0.13.1/"$(TERRAFORM_NAME)

all: package

package:
	git clone https://github.com/packpack/packpack.git packpack
	./packpack/packpack

test: test_$(TRAVIS_OS_NAME)

# Redirect some targets via docker
test_linux: docker_test_debian
coverage: docker_coverage_debian
lto: docker_test_debian
lto_clang11: docker_test_debian_clang11
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
deps_debian:
	apt-get update ${APT_EXTRA_FLAGS} && apt-get install -y -f \
		build-essential cmake coreutils sed \
		libreadline-dev libncurses5-dev libyaml-dev libssl-dev \
		libcurl4-openssl-dev libunwind-dev libicu-dev \
		python python-pip python-setuptools python-dev \
		python-msgpack python-yaml python-argparse python-six python-gevent \
		lcov ruby clang llvm llvm-dev zlib1g-dev autoconf automake libtool

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

deps_debian_jepsen: $(BIN_DIR)/clojure $(BIN_DIR)/lein $(BIN_DIR)/terraform
	apt-get update ${APT_EXTRA_FLAGS} && apt-get install -y -f \
		openjdk-8-jre openjdk-8-jre-headless libjna-java gnuplot graphviz \
		zip unzip openssh-client jq

$(BIN_DIR)/clojure:
	curl $(CLOJURE_URL) | sudo bash

$(BIN_DIR)/lein:
	curl $(LEIN_URL) > $@
	chmod a+x $@

$(BIN_DIR)/terraform:
	apt-get update ${APT_EXTRA_FLAGS} && apt-get install -y -f \
		unzip
	curl -O $(TERRAFORM_URL)
	unzip -o $(TERRAFORM_NAME) terraform -d $(dir $@)
	rm -f $(TERRAFORM_NAME)

# Release

configure_debian:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}

build_debian: configure_debian
	make -j

test_debian_no_deps: build_debian
	cd test && /usr/bin/python test-run.py --force $(TEST_RUN_EXTRA_PARAMS)

test_debian: deps_debian test_debian_no_deps

test_debian_clang11: deps_debian deps_buster_clang_11 test_debian_no_deps

# Debug with coverage

build_coverage_debian:
	cmake . -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
	make -j

test_coverage_debian_no_deps: build_coverage_debian
	# Enable --long tests for coverage
	cd test && /usr/bin/python test-run.py --force $(TEST_RUN_EXTRA_PARAMS) --long
	lcov --compat-libtool --directory src/ --capture --output-file coverage.info.tmp \
		--rc lcov_branch_coverage=1 --rc lcov_function_coverage=1
	lcov --compat-libtool --remove coverage.info.tmp 'tests/*' 'third_party/*' '/usr/*' \
		--rc lcov_branch_coverage=1 --rc lcov_function_coverage=1 --output-file coverage.info
	lcov --list coverage.info
	# coveralls API: https://docs.coveralls.io/api-reference
	@if [ -n "$(COVERALLS_TOKEN)" ]; then \
		echo "Exporting code coverage information to coveralls.io"; \
		gem install coveralls-lcov; \
		echo coveralls-lcov --service-name github-ci --service-job-id $(GITHUB_RUN_ID) \
			--repo-token [FILTERED] coverage.info; \
		coveralls-lcov --service-name github-ci --service-job-id $(GITHUB_RUN_ID) \
			--repo-token $(COVERALLS_TOKEN) coverage.info; \
	fi;

coverage_debian: deps_debian test_coverage_debian_no_deps

# ASAN

build_asan_debian:
	CC=clang-11 CXX=clang++-11 cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DENABLE_WERROR=ON -DENABLE_ASAN=ON -DENABLE_UB_SANITIZER=ON \
		${CMAKE_EXTRA_PARAMS}
	make -j

test_asan_debian_no_deps: build_asan_debian
	# Temporary excluded some tests by issue #4360:
	#  - To exclude tests from ASAN checks the asan/asan.supp file
	#    was set at the build time in cmake/profile.cmake file.
	#  - To exclude tests from LSAN checks the asan/lsan.supp file
	#    was set in environment options to be used at run time.
	cd test && ASAN=ON \
		LSAN_OPTIONS=suppressions=${PWD}/asan/lsan.supp \
		ASAN_OPTIONS=heap_profile=0:unmap_shadow_on_exit=1:detect_invalid_pointer_pairs=1:symbolize=1:detect_leaks=1:dump_instruction_bytes=1:print_suppressions=0 \
		./test-run.py --force $(TEST_RUN_EXTRA_PARAMS)

test_asan_debian: deps_debian deps_buster_clang_11 test_asan_debian_no_deps

# Static build

deps_debian_static:
	# Found that in Debian OS libunwind library built with dependencies to
	# liblzma library, but there is no liblzma static library installed,
	# while liblzma dynamic library exists. So the build dynamicaly has no
	# issues, while static build fails. To fix it we need to install
	# liblzma-dev package with static library only for static build.
	apt-get install -y -f liblzma-dev

test_static_build: deps_debian_static
	CMAKE_EXTRA_PARAMS=-DBUILD_STATIC=ON make -f .travis.mk test_debian_no_deps

# New static build
# builddir used in this target - is a default build path from cmake
# ExternalProject_Add()
test_static_build_cmake_linux:
	cd static-build && cmake -DCMAKE_TARANTOOL_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo;-DENABLE_WERROR=ON" . && \
	make -j && ctest -V
	cd test && /usr/bin/python test-run.py --force \
		--builddir ${PWD}/static-build/tarantool-prefix/src/tarantool-build $(TEST_RUN_EXTRA_PARAMS)

# ###################
# Static Analysis
# ###################

test_debian_docker_luacheck:
	docker run -w ${OOS_SRC_PATH} -v ${PWD}:${OOS_SRC_PATH} --privileged \
		--cap-add=sys_nice --network=host -i ${DOCKER_IMAGE_TARANTOOL} \
		make -f .travis.mk test_debian_luacheck

test_debian_install_luacheck:
	apt update -y
	apt install -y lua5.1 luarocks
	luarocks install luacheck

test_debian_luacheck: test_debian_install_luacheck configure_debian
	make luacheck

# Out-Of-Source build

build_oos:
	mkdir ${OOS_BUILD_PATH} 2>/dev/null || : ; \
		cd ${OOS_BUILD_PATH} && \
		cmake ${OOS_SRC_PATH} ${CMAKE_EXTRA_PARAMS} && \
		make -j

test_oos_no_deps: build_oos
	cd ${OOS_BUILD_PATH}/test && \
		${OOS_SRC_PATH}/test/test-run.py \
			--builddir ${OOS_BUILD_PATH} \
			--vardir ${OOS_BUILD_PATH}/test/var --force

test_oos: deps_debian test_oos_no_deps

test_oos_build:
	docker run --network=host -w ${OOS_SRC_PATH} \
		--mount type=bind,source="${PWD}",target=${OOS_SRC_PATH},readonly,bind-propagation=rslave \
		-i ${DOCKER_IMAGE_TARANTOOL} \
		make -f .travis.mk ${OOS_BUILD_RULE}

#######
# OSX #
#######

# since Python 2 is EOL it's latest commit from tapped local formula is used
OSX_PKGS_MIN=openssl readline curl icu4c libiconv zlib autoconf automake libtool \
	cmake
OSX_PKGS=${OSX_PKGS_MIN} file://$${PWD}/tools/brew_taps/tntpython2.rb

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
	pip install --force-reinstall -r test-run/requirements.txt

deps_osx_github_actions:
	# try to install the packages either upgrade it to avoid of fails
	# if the package already exists with the previous version
	brew install --force ${OSX_PKGS_MIN} || brew upgrade ${OSX_PKGS_MIN}
	pip install --force-reinstall -r test-run/requirements.txt

build_osx:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j


# Limits: Increase the maximum number of open file descriptors on macOS:
#   Travis-ci needs the "ulimit -n <value>" call
#   Gitlab-ci needs the "launchctl limit maxfiles <value>" call
# Also gitlib-ci needs the password to change the limits, while
# travis-ci runs under root user. Limit setup must be in the same
# call as tests runs call.
# Tests: Temporary excluded replication/ suite with some tests
#        from other suites by issues #4357 and #4370
INIT_TEST_ENV_OSX=\
	sudo -S launchctl limit maxfiles ${MAX_FILES} || : ; \
		launchctl limit maxfiles || : ; \
		ulimit -n ${MAX_FILES} || : ; \
		ulimit -n ; \
		sudo -S launchctl limit maxproc ${MAX_PROC} || : ; \
		launchctl limit maxproc || : ; \
		ulimit -u ${MAX_PROC} || : ; \
		ulimit -u ; \
		rm -rf ${OSX_VARDIR}

test_osx_no_deps: build_osx
	${INIT_TEST_ENV_OSX}; \
	cd test && ./test-run.py --vardir ${OSX_VARDIR} --force $(TEST_RUN_EXTRA_PARAMS)

test_osx: deps_osx test_osx_no_deps

test_osx_github_actions: deps_osx_github_actions test_osx_no_deps

# Static macOS build

STATIC_OSX_PKGS=autoconf automake libtool cmake file://$${PWD}/tools/brew_taps/tntpython2.rb
base_deps_osx:
	brew update || echo | /usr/bin/ruby -e \
		"$$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
	brew install --force ${STATIC_OSX_PKGS} || brew upgrade ${STATIC_OSX_PKGS}
	pip install --force-reinstall -r test-run/requirements.txt

# builddir used in this target - is a default build path from cmake
# ExternalProject_Add()
test_static_build_cmake_osx: base_deps_osx
	cd static-build && cmake -DCMAKE_TARANTOOL_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo;-DENABLE_WERROR=ON" . && \
	make -j && ctest -V
	${INIT_TEST_ENV_OSX}; \
	cd test && ./test-run.py --vardir ${OSX_VARDIR} \
		--builddir ${PWD}/static-build/tarantool-prefix/src/tarantool-build \
		--force $(TEST_RUN_EXTRA_PARAMS)


###########
# FreeBSD #
###########

deps_freebsd:
	sudo pkg install -y git cmake gmake icu libiconv \
		python27 py27-yaml py27-six py27-gevent \
		autoconf automake libtool

build_freebsd:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	gmake -j

test_freebsd_no_deps: build_freebsd
	cd test && python2.7 test-run.py --force $(TEST_RUN_EXTRA_PARAMS)

test_freebsd: deps_freebsd test_freebsd_no_deps

# ###################
# Jepsen testing
# ###################

test_jepsen: deps_debian_jepsen
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON -DWITH_JEPSEN=ON
	make run-jepsen
