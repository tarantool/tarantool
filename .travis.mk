#
# Travis CI rules
#

all: package

source:
	git clone https://github.com/packpack/packpack.git packpack
	TARBALL_COMPRESSOR=gz packpack/packpack tarball

package:
	git clone https://github.com/packpack/packpack.git packpack
	./packpack/packpack

test: test_$(TRAVIS_OS_NAME)

deps_linux:
	sudo apt-get update > /dev/null
	sudo apt-get -q -y install binutils-dev python-daemon python-yaml
	sudo pip install six==1.9.0
	sudo pip install gevent==1.1.2

test_linux: deps_linux
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

coverage: deps_linux
	sudo apt-get -q -y install lcov
	cmake . -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
	make -j8
	# Enable --long tests for coverage
	cd test && /usr/bin/python test-run.py --long
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
