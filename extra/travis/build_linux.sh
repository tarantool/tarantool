sudo apt-get update > /dev/null
sudo apt-get -q -y install binutils-dev python-daemon python-yaml
sudo apt-get -q -y install libmysqlclient-dev libpq-dev postgresql-server-dev-all
sudo pip install six==1.9.0
sudo pip install gevent==1.1.2
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j8
