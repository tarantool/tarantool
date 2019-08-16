#
# Travis CI rules
#

DOCKER_IMAGE?=packpack/packpack:debian-stretch
TEST_RUN_EXTRA_PARAMS?=

all: package

package:
	git clone https://github.com/packpack/packpack.git packpack
	./packpack/packpack

test: test_$(TRAVIS_OS_NAME)

# Redirect some targets via docker
test_linux: docker_test_debian
coverage: docker_coverage_debian

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
		-e CC=${CC} \
		-e CXX=${CXX} \
		${DOCKER_IMAGE} \
		make -f .travis.mk $(subst docker_,,$@)

#########
# Linux #
#########

# Depends

# When dependencies in 'deps_debian' or 'deps_buster_clang_8' goal
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
	apt-get update && apt-get install -y -f \
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

build_debian:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j

test_debian_no_deps: build_debian
	cd test && /usr/bin/python test-run.py --force $(TEST_RUN_EXTRA_PARAMS)

test_debian: deps_debian test_debian_no_deps

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

#######
# OSX #
#######

deps_osx:
	brew update
	brew install openssl readline curl icu4c zlib autoconf automake libtool --force
	python2 -V || brew install python2 --force
	curl --silent --show-error --retry 5 https://bootstrap.pypa.io/get-pip.py >get-pip.py
	python get-pip.py --user
	pip install --user --force-reinstall -r test-run/requirements.txt

build_osx:
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_WERROR=ON ${CMAKE_EXTRA_PARAMS}
	make -j

test_osx_no_deps: build_osx
	# Increase the maximum number of open file descriptors on macOS
	sudo sysctl -w kern.maxfiles=20480 || :
	sudo sysctl -w kern.maxfilesperproc=20480 || :
	sudo launchctl limit maxfiles 20480 || :
	ulimit -S -n 20480 || :
	ulimit -n
	cd test && ./test-run.py --force $(TEST_RUN_EXTRA_PARAMS) unit/ app/ app-tap/ box/ box-tap/

test_osx: deps_osx test_osx_no_deps

###########
# FreeBSD #
###########

deps_freebsd:
	sudo pkg install -y git cmake gmake gcc coreutils \
		readline ncurses libyaml openssl libunwind icu \
		python27 py27-pip py27-setuptools py27-daemon \
		py27-yaml py27-argparse py27-six py27-gevent \
		gdb bash autoconf automake libtool

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
MAJOR_VERSION=$(word 1,$(subst ., ,$(TRAVIS_BRANCH)))
MINOR_VERSION=$(word 2,$(subst ., ,$(TRAVIS_BRANCH)))
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
