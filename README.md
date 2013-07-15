TARANTOOL, http://tarantool.org

Tarantool is an efficient in-memory NoSQL database and a
Lua application server, blended.

Key features of the system:
 * flexible data model
 * multiple index types: HASH, TREE, BITSET
 * optional persistency and strong data durability
 * log streaming replication
 * Lua functions, procedures, triggers, with
   rich access to database API, JSON support,
   inter-procedure and network communication libraries
 * a command line client supporting simple SQL,
   a native Lua console and Memcached text protocol.

Tarantool is ideal for data-enriched components of 
scalable Web architecture: traditional database caches, queue
servers, in-memory data store for hot data, and so on.

Supported platforms are Linux/x86 and FreeBSD/x86, Mac OS X.

COMPILATION AND INSTALL

Tarantool is written in C and C++.
To build, you will need GCC or Apple CLang compiler.

CMake is used for configuration management.
3 standard CMake build types are supported:
 * Debug -- used by project maintainers
 * RelWithDebugInfo -- the most common release configuration,
 also provides debugging capabilities
 * Release -- use only if the highest performance is required

The only external library dependency is readline: libreadline-dev
is required to build the command line client.

There are two OPTIONAL dependencies: 
- uuid-dev. It is required for box.uuid_* functions.
- GNU bfd (part of GNU binutils). It's used to print 
a stack trace after a crash.

Please follow these steps to compile Tarantool:

tarantool $ git submodule init; git submodule update # if compiling from git
tarantool $ cmake .
tarantool $ make

To use a different release type, say, RelWithDebugInfo, use:

tarantool $ cmake . -DCMAKE_BUILD_TYPE=RelWithDebugInfo

Additional build options can be set similarly:

tarantool $ cmake . -DCMAKE_BUILD_TYPE=RelWithDebugInfo -DENABLE_CLIENT=true

-- builds the command line client.

'make' creates tarantool_box executable in directory
src/box and tarantool executable in client/tarantool.

There is no 'make install' goal, but no installation
is required either.
Tarantool regression testing framework (test/test-run.py) is the
simplest way to setup and start the server, but it requires a few
additional Python modules:
 * daemon
 * pyyaml

Once all pre-requisites are installed, try:

tarantool $ cd test && ./test-run.py --suite box --start-and-exit

This will create a 'var' subdirectory in directory 'test',
populate it with necessary files, and
start the server. To connect, you could use
a simple command-line client:

'''
tarantool $ ./test/tarantool
'''

Alternatively, if a customized server configuration is required,
you could follow these steps:

```
tarantool $ emacs cfg/tarantool.cfg # edit the configuration
# Initialize the storage directory, path to this directory
# is specified in the configuration file:
tarantool $ src/box/tarantool_box --config cfg/tarantool.cfg --init-storage
#
# run
tarantool $ src/box/tarantool_box --config cfg/tarantool.cfg
```

Please report bugs at http://bugs.launchpad.net/tarantool or
http://github.com/tarantool/tarantool/issues
We also warmly welcome your feedback in the discussion mailing
list, tarantool@googlegroups.com.

Thank you for your interest in Tarantool!
