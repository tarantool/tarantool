#
# Travis CI rules
#

DOCKER_IMAGE:=packpack/packpack:ubuntu-xenial

all: package

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
		libcurl4-openssl-dev binutils-dev \
		python python-pip python-setuptools python-dev \
		python-msgpack python-yaml python-argparse python-six python-gevent \
		lcov ruby

test_ubuntu: deps_ubuntu
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	make -j8
	cd test && /usr/bin/python test-run.py

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
	cd test && python test-run.py unit/ app/ app-tap/ box/ box-tap/

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
