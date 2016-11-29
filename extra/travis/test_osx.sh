echo "Increase the maximum number of open file descriptors on macOS"
NOFILE=20480
sudo sysctl -w kern.maxfiles=$NOFILE
sudo sysctl -w kern.maxfilesperproc=$NOFILE
sudo launchctl limit maxfiles $NOFILE $NOFILE
ulimit -S -n $NOFILE
ulimit -n

cd test && python test-run.py
