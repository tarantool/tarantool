#
# Travis CI rules
#

DOCKER_IMAGE:=packpack/packpack:ubuntu-zesty

all: package

source:
	git clone https://github.com/packpack/packpack.git packpack
	TARBALL_COMPRESSOR=gz packpack/packpack tarball

package:
	git clone https://github.com/packpack/packpack.git packpack
	./packpack/packpack

test: test_$(TRAVIS_OS_NAME)

# Redirect some targets via docker
test_linux: docker_test_ubuntu
coverage: docker_coverage_ubuntu

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
		${DOCKER_IMAGE} \
		make -f .travis.mk $(subst docker_,,$@)

deps_ubuntu:
	sudo apt-get update && apt-get install -y -f \
		build-essential cmake coreutils sed \
		libreadline-dev libncurses5-dev libyaml-dev libssl-dev \
		libcurl4-openssl-dev libunwind-dev \
		python python-pip python-setuptools python-dev \
		python-msgpack python-yaml python-argparse python-six python-gevent \
		lcov ruby

test_ubuntu: deps_ubuntu
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	make -j8
	cd test && /usr/bin/python test-run.py -j -1

deps_osx:
	brew install openssl readline --force
	sudo pip install python-daemon PyYAML
	sudo pip install six==1.9.0
	sudo pip install gevent==1.1.2

test_osx: deps_osx
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	# Increase the maximum number of open file descriptors on macOS
	sudo sysctl -w kern.maxfiles=20480 || :
	sudo sysctl -w kern.maxfilesperproc=20480 || :
	sudo launchctl limit maxfiles 20480 || :
	ulimit -S -n 20480 || :
	ulimit -n
	make -j8
	cd test && python test-run.py -j -1 unit/ app/ app-tap/ box/ box-tap/

coverage_ubuntu: deps_ubuntu
	cmake . -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
	make -j8
	# Enable --long tests for coverage
	cd test && /usr/bin/python test-run.py -j -1 --long
	lcov --compat-libtool --directory src/ --capture --output-file coverage.info.tmp
	lcov --compat-libtool --remove coverage.info.tmp 'tests/*' 'third_party/*' '/usr/*' \
		--output-file coverage.info
	lcov --list coverage.info
	@if [ -n "$(COVERALLS_TOKEN)" ]; then \
		echo "Exporting code coverage information to coveralls.io"; \
		gem install coveralls-lcov; \
		echo coveralls-lcov --repo-token [FILTERED] coverage.info; \
		coveralls-lcov --repo-token $(COVERALLS_TOKEN) coverage.info; \
	fi;
