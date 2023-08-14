#
# CI testing rules
#

SRC_DIR = .
BUILD_DIR ?= .
STATIC_DIR = static-build
STATIC_BIN_DIR = ${STATIC_DIR}/tarantool-prefix/src/tarantool-build
LUAJIT_TEST_BUILD_DIR = ${BUILD_DIR}

# `nproc`                   - for Linux
# `sysctl -n hw.logicalcpu` - for OSX
# `sysctl -n hw.ncpu`       - for FreeBSD (deprecated in OSX)
NPROC ?= $(shell nproc || sysctl -n hw.logicalcpu || sysctl -n hw.ncpu)

MAX_PROCS ?= 2048
MAX_FILES ?= 4096

VARDIR ?= /tmp/t
TEST_RUN_PARAMS = --builddir ${PWD}/${BUILD_DIR}

COVERITY_DIR = cov-int
COVERITY_URL = https://scan.coverity.com/builds?project=tarantool%2Ftarantool

CMAKE = ${CMAKE_ENV} cmake -S ${SRC_DIR} -B ${BUILD_DIR}
CMAKE_BUILD = ${CMAKE_BUILD_ENV} cmake --build ${BUILD_DIR} --parallel ${NPROC}

.PHONY: configure
configure:
	${CMAKE} ${CMAKE_PARAMS} ${CMAKE_EXTRA_PARAMS}

# Static Analysis

.PHONY: luacheck
luacheck: configure
	${CMAKE_BUILD} --target luacheck

# Building

.PHONY: build
build: configure
	${CMAKE_BUILD}
	if [ "${CTEST}" = "true" ]; then cd ${BUILD_DIR} && ctest -V; fi

# Testing

.PHONY: run-luajit-test
run-luajit-test:
	${LUAJIT_TEST_ENV} cmake --build ${LUAJIT_TEST_BUILD_DIR} --parallel ${NPROC} --target LuaJIT-test

.PHONY: install-test-deps
install-test-deps:
	python3 -m pip install -r test-run/requirements.txt

.PHONY: run-test
run-test: install-test-deps
	cd test && ${TEST_RUN_ENV} ./test-run.py --force --vardir ${VARDIR} ${TEST_RUN_PARAMS} ${TEST_RUN_EXTRA_PARAMS}

.PHONY: run-perf-test
run-perf-test:
	cmake --build ${BUILD_DIR} --parallel
	cmake --build ${BUILD_DIR} --target test-perf

##############################
# Linux                      #
##############################

# Release build

.PHONY: test-release
test-release: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                             -DENABLE_WERROR=ON \
                             -DTEST_BUILD=ON

test-release: build run-luajit-test run-test

.PHONY: test-perf
test-perf: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                          -DENABLE_WERROR=ON \
                          -DTEST_BUILD=ON

test-perf: build run-perf-test

# Release ASAN build

.PHONY: test-release-asan
# FIBER_STACK_SIZE=640Kb: The default value of fiber stack size
# is 512Kb, but several tests in test/PUC-Rio-Lua-5.1-test suite
# in the LuaJIT repo (e.g. some cases with deep recursion in
# errors.lua or pm.lua) have already been tweaked according to the
# limitations mentioned in #5782, but the crashes still occur
# while running LuaJIT tests with ASan support enabled.
# Experiments once again confirm the notorious quote that "640 Kb
# ought to be enough for anybody".
test-release-asan: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                                  -DENABLE_WERROR=ON \
                                  -DENABLE_ASAN=ON \
                                  -DENABLE_UB_SANITIZER=ON \
                                  -DFIBER_STACK_SIZE=640Kb \
                                  -DENABLE_FUZZER=ON \
                                  -DTEST_BUILD=ON
# Some checks are temporary suppressed in the scope of the issue
# https://github.com/tarantool/tarantool/issues/4360:
#   - ASAN: to suppress failures of memory error checks caught while tests run, the asan/asan.supp
#     file is used. It is set as a value for the `-fsanitize-blacklist` option at the build time in
#     the cmake/profile.cmake file.
#   - LSAN: to suppress failures of memory leak checks caught while tests run, the asan/lsan.supp
#     file is used.
test-release-asan: TEST_RUN_ENV = ASAN=ON \
                                  LSAN_OPTIONS=suppressions=${PWD}/asan/lsan.supp \
                                  ASAN_OPTIONS=heap_profile=0:unmap_shadow_on_exit=1:$\
                                               detect_invalid_pointer_pairs=1:symbolize=1:$\
                                               detect_leaks=1:dump_instruction_bytes=1:$\
                                               print_suppressions=0
test-release-asan: LUAJIT_TEST_ENV = LSAN_OPTIONS=suppressions=${PWD}/asan/lsan.supp \
                                     ASAN_OPTIONS=detect_invalid_pointer_pairs=1:$\
                                                  detect_leaks=1:$\
                                                  dump_instruction_bytes=1:$\
                                                  heap_profile=0:$\
                                                  print_suppressions=0:$\
                                                  symbolize=1:$\
                                                  unmap_shadow_on_exit=1
test-release-asan: build run-luajit-test run-test

# Debug build

.PHONY: test-debug
test-debug: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=Debug \
                           -DTEST_BUILD=ON
test-debug: build run-luajit-test run-test

# Static build

.PHONY: test-static
test-static: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                            -DENABLE_WERROR=ON \
                            -DBUILD_STATIC=ON \
                            -DTEST_BUILD=ON
test-static: build run-luajit-test run-test

# Static build (cmake)

.PHONY: test-static-cmake
test-static-cmake: SRC_DIR = ${STATIC_DIR}
test-static-cmake: BUILD_DIR = ${STATIC_DIR}
test-static-cmake: CMAKE_PARAMS = -DCMAKE_TARANTOOL_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo;-DENABLE_WERROR=ON;-DTEST_BUILD=ON"
test-static-cmake: CTEST = true
test-static-cmake: LUAJIT_TEST_BUILD_DIR = ${STATIC_BIN_DIR}
test-static-cmake: TEST_RUN_PARAMS = --builddir ${PWD}/${STATIC_BIN_DIR}
test-static-cmake: build run-luajit-test run-test

# Coverage build

.PHONY: test-coverage
test-coverage: CMAKE_PARAMS = -G Ninja \
                              -DCMAKE_BUILD_TYPE=Debug \
                              -DENABLE_GCOV=ON \
                              -DTEST_BUILD=ON
test-coverage: TEST_RUN_PARAMS += --long
test-coverage: OUTPUT_FILE = coverage.info
test-coverage: build run-luajit-test run-test
	lcov --capture \
	     --compat-libtool \
	     --directory ${BUILD_DIR}/src/ \
	     --output-file ${OUTPUT_FILE} \
	     --rc lcov_function_coverage=1 \
	     --rc lcov_branch_coverage=1 \
	     --exclude '/usr/*' \
	     --exclude '*/build/*' \
	     --exclude '*/test/*' \
	     --exclude '*/third_party/*'
	lcov --list ${OUTPUT_FILE}

##############################
# OSX                        #
##############################

.PHONY: prebuild-osx
prebuild-osx:
	sysctl vm.swapusage

.PHONY: pretest-osx
pretest-osx:
	ulimit -u ${MAX_PROCS} || : && ulimit -u
	ulimit -n ${MAX_FILES} || : && ulimit -n

# Release build

.PHONY: test-osx-release
test-osx-release: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                                 -DENABLE_WERROR=ON \
                                 -DTEST_BUILD=ON
test-osx-release: prebuild-osx build run-luajit-test pretest-osx run-test

# Debug build

.PHONY: test-osx-debug
test-osx-debug: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=Debug \
                               -DTEST_BUILD=ON
test-osx-debug: prebuild-osx build run-luajit-test pretest-osx run-test

# Static build

.PHONY: test-osx-static-cmake
test-osx-static-cmake: SRC_DIR = ${STATIC_DIR}
test-osx-static-cmake: BUILD_DIR = ${STATIC_DIR}
test-osx-static-cmake: CMAKE_PARAMS = -DCMAKE_TARANTOOL_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo;-DENABLE_WERROR=ON;-DTEST_BUILD=ON"
test-osx-static-cmake: CTEST = true
test-osx-static-cmake: LUAJIT_TEST_BUILD_DIR = ${STATIC_BIN_DIR}
test-osx-static-cmake: TEST_RUN_PARAMS = --builddir ${PWD}/${STATIC_BIN_DIR}
test-osx-static-cmake: prebuild-osx build run-luajit-test pretest-osx run-test

##############################
# FreeBSD                    #
##############################

.PHONY: prebuild-freebsd
prebuild-freebsd:
	if [ "$$(swapctl -l | wc -l)" != "1" ]; then swapoff -a; fi
	swapctl -l

# Release build

.PHONY: test-freebsd-release
test-freebsd-release: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                                     -DENABLE_WERROR=ON \
                                     -DTEST_BUILD=ON
test-freebsd-release: prebuild-freebsd build run-luajit-test run-test

##############################
# Jepsen testing             #
##############################

.PHONY: prebuild-jepsen
prebuild-jepsen:
	# Jepsen build uses git commands internally, like command `git stash --all` that fails w/o
	# git configuration setup.
	git config --get user.name || git config --global user.name "Nodody User"
	git config --get user.email || git config --global user.email "nobody@nowhere.com"

.PHONY: test-jepsen
test-jepsen: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                            -DENABLE_WERROR=ON \
                            -DWITH_JEPSEN=ON \
                            -DTEST_BUILD=ON
test-jepsen: configure prebuild-jepsen
	${CMAKE_BUILD} --target run-jepsen

##############################
# Coverity testing           #
##############################

.PHONY: build-coverity
build-coverity: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                               -DENABLE_WERROR=ON \
                               -DTEST_BUILD=ON
build-coverity: CMAKE_BUILD_ENV = PATH=${PATH}:/cov-analysis/bin cov-build --dir ${COVERITY_DIR}
build-coverity: configure
	${CMAKE_BUILD}

.PHONY: test-coverity
test-coverity: build-coverity
	tar czvf tarantool.tgz ${COVERITY_DIR}
	if [ -n "$${COVERITY_TOKEN}" ]; then \
		echo "Exporting code coverity information to scan.coverity.com"; \
		curl --location \
		     --fail \
		     --silent \
		     --show-error \
		     --retry 5 \
		     --retry-delay 5 \
		     --form token=$${COVERITY_TOKEN} \
		     --form email=tarantool@tarantool.org \
		     --form file=@tarantool.tgz \
		     --form version=$(shell git describe HEAD) \
		     --form description="Tarantool Coverity" \
		     ${COVERITY_URL}; \
	else \
		echo "Coverity token is not provided"; \
		exit 1; \
	fi

##############################
# LuaJIT integration testing #
##############################

test-luajit-Linux-x86_64: build run-luajit-test run-test

test-luajit-Linux-ARM64: build run-luajit-test run-test

test-luajit-macOS-x86_64: prebuild-osx build run-luajit-test pretest-osx run-test

test-luajit-macOS-ARM64: prebuild-osx build run-luajit-test pretest-osx run-test
