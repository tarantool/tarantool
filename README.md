# Tarantool

[![Build Status][travis-badge]][travis-url]
[![Build Status][gitlab-ci-badge]][gitlab-ci-url]
[![Code Coverage][coverage-badge]][coverage-url]
[![Telegram][telegram-badge]][telegram-url]
[![Slack][slack-badge]][slack-url]
[![Gitter][gitter-badge]][gitter-url]
[![Google Groups][groups-badge]][groups-url]

https://tarantool.io/en/

Patch submissions and discussion of particular patches https://lists.tarantool.org/mailman/listinfo/tarantool-patches/

General development discussions https://lists.tarantool.org/mailman/listinfo/tarantool-discussions/

Tarantool is an in-memory database and application server.

Key features of the application server:
 * 100% compatible drop-in replacement for Lua 5.1,
   based on LuaJIT 2.1.
   Simply use `#!/usr/bin/tarantool` instead of
   `#!/usr/bin/lua` in your script.
 * full support for Lua modules and a rich set of
   own modules, including cooperative multitasking,
   non-blocking I/O, access to external databases, etc

Key features of the database:
 * ANSI SQL, including views, joins, referential
   and check constraints
 * MsgPack data format and MsgPack based
   client-server protocol
 * two data engines: 100% in-memory with
   optional persistence and an own implementation of LSM-tree, 
   to use with large data sets
 * multiple index types: HASH, TREE, RTREE, BITSET
 * asynchronous master-master replication
 * authentication and access control
 * the database is just a C extension to the
   application server and can be turned off

Supported platforms are Linux/x86, FreeBSD/x86 and OpenBSD/x86, Mac OS X.

Tarantool is ideal for data-enriched components of
scalable Web architecture: queue servers, caches,
stateful Web applications.

To download and install Tarantool as a binary package for your OS, please visit
https://tarantool.io/en/download/.

To build Tarantool from source, see detailed instructions in the Tarantool
documentation at https://tarantool.io/en/doc/2.1/dev_guide/building_from_source/.

Please report bugs at https://github.com/tarantool/tarantool/issues
We also warmly welcome your feedback in the discussion mailing
list, tarantool@googlegroups.com.

Thank you for your interest in Tarantool!

[travis-badge]: https://api.travis-ci.org/tarantool/tarantool.svg?branch=master
[travis-url]: https://travis-ci.org/tarantool/tarantool
[gitlab-ci-badge]: https://gitlab.com/tarantool/tarantool/badges/master/pipeline.svg
[gitlab-ci-url]: https://gitlab.com/tarantool/tarantool/commits/master
[coverage-badge]: https://coveralls.io/repos/github/tarantool/tarantool/badge.svg?branch=master
[coverage-url]: https://coveralls.io/github/tarantool/tarantool?branch=master
[groups-badge]: https://img.shields.io/badge/Google-Groups-orange.svg
[groups-url]: https://groups.google.com/forum/#!forum/tarantool
[telegram-badge]: https://img.shields.io/badge/Telegram-join%20chat-blue.svg
[telegram-url]: http://telegram.me/tarantool
[slack-badge]: https://img.shields.io/badge/Slack-join%20chat-lightgrey.svg
[slack-url]: http://slack.tarantool.org/
[gitter-badge]: https://badges.gitter.im/Join%20Chat.svg
[gitter-url]: https://gitter.im/tarantool/tarantool
