#
# Travis rules
#

travis_deps_linux:
	sudo apt-get update > /dev/null
	sudo apt-get -q -y install binutils-dev python-daemon python-yaml
	sudo pip install six==1.9.0
	sudo pip install gevent==1.1.2

travis_test_linux: travis_deps_linux
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	make -j8
	cd test && /usr/bin/python test-run.py

travis_coverage: travis_deps_linux
	cmake . -DCMAKE_BUILD_TYPE=Debug -DENABLE_GCOV=ON
	make -j8
	# Enable --long tests for coverage
	cd test && /usr/bin/python test-run.py --long

travis_deps_osx:
	brew install openssl readline --force
	sudo pip install python-daemon PyYAML
	sudo pip install six==1.9.0
	sudo pip install gevent==1.1.2

travis_test_osx: travis_deps_osx
	cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
	make -j8
	cd test && python test-run.py unit/ app/ app-tap/ box/ box-tap/
