#
# Travis CI rules
#

DOCKER_IMAGE:=packpack/packpack:debian-stretch

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
		-e TRAVIS_JOB_ID=${TRAVIS_JOB_ID} \
		${DOCKER_IMAGE} \
		make -f .travis.mk $(subst docker_,,$@)

deps_ubuntu:
	sudo apt-get update && apt-get install -y -f \
		build-essential cmake coreutils sed \
		libreadline-dev libncurses5-dev libyaml-dev libssl-dev \
		libcurl4-openssl-dev libunwind-dev libicu-dev \
		python python-pip python-setuptools python-dev \
		python-msgpack python-yaml python-argparse python-six python-gevent \
		lcov ruby

test_ubuntu: deps_ubuntu
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	make -j8
	cd test && /usr/bin/python test-run.py -j -1

deps_osx:
	brew update
	brew install openssl readline curl icu4c --force

test_osx: deps_osx
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	# Increase the maximum number of open file descriptors on macOS
	sudo sysctl -w kern.maxfiles=20480 || :
	sudo sysctl -w kern.maxfilesperproc=20480 || :
	sudo launchctl limit maxfiles 20480 || :
	ulimit -S -n 20480 || :
	ulimit -n
	make -j8
	virtualenv ./test-env && \
	. ./test-env/bin/activate && \
	curl --silent --show-error --retry 5 https://bootstrap.pypa.io/get-pip.py | python && \
	pip --version && \
	pip install -r test-run/requirements.txt && \
	cd test && python test-run.py -j -1 unit/ app/ app-tap/ box/ box-tap/ && \
	deactivate

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
		echo coveralls-lcov --service-name travis-ci --service-job-id $(TRAVIS_JOB_ID) --repo-token [FILTERED] coverage.info; \
		coveralls-lcov --service-name travis-ci --service-job-id $(TRAVIS_JOB_ID) --repo-token $(COVERALLS_TOKEN) coverage.info; \
	fi;

source:
	git clone https://github.com/packpack/packpack.git packpack
	TARBALL_COMPRESSOR=gz packpack/packpack tarball

source_deploy:
	pip install awscli --user
	aws --endpoint-url "${AWS_S3_ENDPOINT_URL}" s3 \
		cp build/*.tar.gz "s3://tarantool-${TRAVIS_BRANCH}-src/" \
		--acl public-read
