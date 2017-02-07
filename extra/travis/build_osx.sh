brew install openssl readline --force
sudo pip install python-daemon PyYAML
sudo pip install six==1.9.0
sudo pip install gevent==1.1.2
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j8
