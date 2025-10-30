#
# CI testing rules
#

SRC_DIR = .
BUILD_DIR ?= .
STATIC_DIR = static-build
STATIC_BIN_DIR = ${STATIC_DIR}/tarantool-prefix/src/tarantool-build
LUAJIT_TEST_BUILD_DIR = ${BUILD_DIR}

MAX_PROCS ?= 2048
MAX_FILES ?= 4096

N_TRIALS ?= 128
COMMIT_RANGE ?= master..HEAD

VARDIR ?= /tmp/t

CMAKE = ${CMAKE_ENV} cmake -S ${SRC_DIR} -B ${BUILD_DIR}
CMAKE_BUILD = cmake --build ${BUILD_DIR} --parallel

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

# Testing

.PHONY: run-luajit-test
run-luajit-test:
	${LUAJIT_TEST_ENV} cmake --build ${LUAJIT_TEST_BUILD_DIR} --parallel --target LuaJIT-test

.PHONY: install-test-deps
install-test-deps:
	python3 -m pip install -r test-run/requirements.txt

.PHONY: run-test
run-test: install-test-deps
	cmake --build ${BUILD_DIR} --parallel --target test-force

.PHONY: run-test-ctest
run-test-ctest:
	cmake --build ${BUILD_DIR} --target test-force-ctest

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
test-perf: CMAKE_ENV = BENCH_CMD="${BENCH_CMD}"
test-perf: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                          -DENABLE_BUNDLED_LIBBENCHMARK=ON \
                          -DENABLE_WERROR=ON \
                          -DTEST_BUILD=ON

test-perf: build run-perf-test

.PHONY: test-perf-aggregate
test-perf-aggregate:
	cmake --build ${BUILD_DIR} --target test-perf-aggregate

# *_ASAN variables are common part of respective variables for all ASAN builds.
CMAKE_PARAMS_ASAN = -DENABLE_WERROR=ON \
                    -DENABLE_ASAN=ON \
                    -DENABLE_UB_SANITIZER=ON \
                    -DTEST_BUILD=ON

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
test-release-asan: CMAKE_PARAMS = ${CMAKE_PARAMS_ASAN} \
                                  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                                  -DFIBER_STACK_SIZE=640Kb
test-release-asan: build run-luajit-test run-test

# Debug ASAN build

.PHONY: test-debug-asan
# We need even larger fiber stacks in ASAN debug build for luajit tests
# to pass. Value twice as big as in ASAN release is just a wild guess.
test-debug-asan: CMAKE_PARAMS = ${CMAKE_PARAMS_ASAN} \
                                -DCMAKE_BUILD_TYPE=Debug \
                                -DFIBER_STACK_SIZE=1280Kb
test-debug-asan: build run-luajit-test run-test

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
test-static-cmake: CMAKE_PARAMS = -DCMAKE_TARANTOOL_ARGS="-DCMAKE_BUILD_TYPE=RelWithDebInfo;-DENABLE_WERROR=ON;-DTEST_BUILD=ON"
test-static-cmake: build run-test-ctest

# Coverage build

.PHONY: test-coverage
test-coverage: CMAKE_PARAMS = -G Ninja \
                              -DCMAKE_BUILD_TYPE=Debug \
                              -DENABLE_GCOV=ON \
                              -DTEST_BUILD=ON
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

# Flaky catching workflow build

.PHONY: test-debug-flaky
test-debug-flaky: CMAKE_PARAMS = -DCMAKE_BUILD_TYPE=Debug \
                                 -DTEST_BUILD=ON
test-debug-flaky: LUA_PATTERN = "./*/*test.lua"
test-debug-flaky: UNITTEST_PATTERN = "./unit/*.c*"

test-debug-flaky: build install-test-deps
	cd test && \
	TESTS=$$(git diff --relative=test --name-only \
	         ${COMMIT_RANGE} -- ${LUA_PATTERN} ${UNITTEST_PATTERN} | \
	         sed 's/\.\(c\|cpp\|cc\)$$//' | grep . | sort -u) && \
	(([ -n "$${TESTS}" ] && ${TEST_RUN_ENV} \
	  ./test-run.py --force \
	                --vardir ${VARDIR} \
	                --retries=0 \
	                --repeat=${N_TRIALS} \
	                ${TEST_RUN_PARAMS} ${TEST_RUN_EXTRA_PARAMS} \
	                $${TESTS}) || \
	 [ ! -n "$${TESTS}" ])


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
test-osx-static-cmake: LUAJIT_TEST_BUILD_DIR = ${STATIC_BIN_DIR}
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
# LuaJIT integration testing #
##############################

test-luajit-Linux-x86_64: build run-luajit-test

test-luajit-Linux-ARM64: build run-luajit-test

test-luajit-macOS-x86_64: prebuild-osx build run-luajit-test

test-luajit-macOS-ARM64: prebuild-osx build run-luajit-test
