# tarantool

[![Build Status](https://travis-ci.org/tarantool/tarantool.png?branch=1.7)](https://travis-ci.org/tarantool/tarantool)
[![Coverage Status](https://coveralls.io/repos/github/tarantool/tarantool/badge.svg?branch=1.7)](https://coveralls.io/github/tarantool/tarantool?branch=1.7)
[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/tarantool/tarantool?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

http://tarantool.org

Tarantool is an in-memory database and application server.

Key features of the application server:
 * 100% compatible drop-in replacement for Lua 5.1,
   based on LuaJIT 2.0.
   Simply use #!/usr/bin/tarantool instead of
   #!/usr/bin/lua in your script.
 * full support for Lua modules and a rich set of
   own modules, including cooperative multitasking,
   non-blocking I/O, access to external databases, etc

Key features of the database:
 * MsgPack data format and MsgPack based
   client-server protocol
 * two data engines: 100% in-memory with
   optional persistence and a 2-level disk-based
   B-tree, to use with large data sets
 * multiple index types: HASH, TREE, BITSET
 * asynchronous master-master replication
 * authentication and access control
 * the database is just a C extension to the
   app server and can be turned off

Supported platforms are Linux/x86 and FreeBSD/x86, Mac OS X.

Tarantool is ideal for data-enriched components of
scalable Web architecture: queue servers, caches,
stateful Web applications.

To download and install Tarantool as a binary package for your OS, please visit
https://tarantool.org/download.html.

To build Tarantool from source, see detailed instructions in the Tarantool
documentation at https://tarantool.org/doc/dev_guide/building_from_source.html.

Please report bugs at http://github.com/tarantool/tarantool/issues
We also warmly welcome your feedback in the discussion mailing
list, tarantool@googlegroups.com.

Thank you for your interest in Tarantool!
