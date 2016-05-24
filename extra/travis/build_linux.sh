sudo apt-get update > /dev/null
sudo apt-get -q -y install binutils-dev python-daemon python-yaml
sudo apt-get -q -y install libmysqlclient-dev libpq-dev postgresql-server-dev-all
sudo pip install six==1.9.0
sudo pip install gevent
sudo pip install geventconnpool
CMAKE_OPTS=""
if [ -n "${COVERALLS_TOKEN}" ] && [ "${CC}" = "gcc" ]; then
    echo "Code coverage analysis is enabled"
    sudo apt-get -q -y install lcov
    gem install coveralls-lcov
    CMAKE_OPTS="-DENABLE_GCOV=ON"
else
    COVERALLS_TOKEN=""
fi
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo ${CMAKE_OPTS}
make -j8

cd test
/usr/bin/python test-run.py
[ $? -eq 0 ] || exit $?
cd ..

if [ -n "${COVERALLS_TOKEN}" ]; then
    echo "Collecting code coverage information"
    # http://gronlier.fr/blog/2015/01/adding-code-coverage-to-your-c-project/
    LCOV_FILE="$(pwd)/coverage.info"
    # Capture coverage info
    lcov --directory src/ --capture --output-file ${LCOV_FILE}.tmp
    # Filter out system, test and third-party code
    lcov --remove ${LCOV_FILE}.tmp 'tests/*' 'third_party/*' '/usr/*' \
        --output-file ${LCOV_FILE}
    rm -f ${LCOV_FILE}.tmp
    echo "Exporting code coverage information to coveralls.io"
    # Upload to coveralls.io
    coveralls-lcov --repo-token ${COVERALLS_TOKEN} ${LCOV_FILE}
fi
