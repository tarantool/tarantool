sudo pip install python-daemon PyYAML
sudo pip install six==1.9.0
sudo pip install gevent
sudo pip install geventconnpool
cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo
make -j8
