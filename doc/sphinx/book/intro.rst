-------------------------------------------------------------------------------
                             Intro
-------------------------------------------------------------------------------

===============================================================================
                        Tarantool: an overview
===============================================================================

Tarantool is a Lua application server integrated with a database management system.
It has a "fiber" model which means that many applications can run simultaneously on
a single thread, while the Tarantool server can run multiple threads for input-output
and background maintenance. It integrates the LuaJIT -- "Just In Time" -- Lua compiler,
Lua libraries for most common applications, and the Tarantool Database Server which
is an established NoSQL DBMS. Thus it serves all the purposes that have made node.js
and Twisted popular in other environments, with the additional twist that it has a
data persistence level.

The code is free. The open-source license is `BSD license`_. The supported platforms
are GNU/Linux, Mac OS and FreeBSD.

Tarantool database is deeply integrated with the application server. On
the surface, Tarantool is simply a Lua language interpreter, and the database
is one of many built-in Lua packages. But the exposed database API not only 
allows to persist Lua objects to disk, but to manage object collections, create
or drop secondary keys, configure and monitor replication, perform controlled
fail-over, execute Lua code upon database events. 
Remote database instances are accessible transparently via remote
procedure invocation API.

Unlike popular application development frameworks based on a "reactor" pattern,
networking in server-side Lua is sequential, yet very efficient, as it is built
on top of the **cooperative multitasking** environment that Tarantool itself
uses. A key feature is that the functions can access and modify databases
atomically.  Thus some developers look at it as a DBMS with a popular stored
procedure language, while others look at it as a replacement for multiple
components of multi-tier Web application architectures. Performance is a few
thousand transactions per second on a laptop, scalable upwards or outwards to
server farms.

Tarantool data storage is built around **storage engine** concept, when
different sets of algorithms and data structures can be used for different
collections of objects. Two storage engines are built-in: in-memory engine,
which represents 100% of data and indexes in RAM, and a two-level B-tree,
for data sets exceeding the amount of available RAM from 10 to up to 1000
times. All storage engines in Tarantool support transactions and
replication by using a common **write ahead log**. This ensures consistency
and crash safety of the persistent state. The logging subsystem supports
group commit.

**Tarantool in-memory engine is lock-free**. Instead of the operating system's
concurrency primitives, such as mutexes, it uses cooperative multitasking to
handle thousands of connections simultaneously. There is a fixed number of
independent execution threads. The threads do not share state. Instead they
exchange data using low-overhead message queues. While this approach limits the
number of cores that the server will use, it removes competition for the memory
bus and ensures peak scalability of memory access and network throughput. CPU
utilization of a typical highly-loaded Tarantool server is under 10%.

**Tarantool disk-based engine** is a fusion of ideas from modern filesystems, 
log-structured merge trees and classical B-trees. All data is organized
into **branches**, each branch is represented by a file on disk. Branch 
size is a configuration option and normally is around 64MB. Each 
branch is a collection of pages, serving different purposes. Pages 
in a fully merged branch contain non-overlapping ranges of keys. A branch
can be partially merged if there were a lot of changes in its key range
recently. In that case some pages represent new keys and values in the
branch. The disk-based engine is append only: new data never overwrites
the old.

Unlike most NoSQL DBMSs, Tarantool supports **secondary index keys** as well as
primary keys, and **multi-part index keys**. The possible index types are HASH,
TREE, BITSET, and RTREE.

Tarantool supports **asynchronous replication**, locally or to remote hosts. 
The replication architecture can be **master-master**, that is, many nodes may
both handle the loads and receive what others have handled, for the same data
sets.

Tarantool's creator and biggest user is `Mail.Ru`_, the largest internet
company in Russia, with 30 million users, 25 million emails per day, and a web
site whose Alexa global rank is in the `top 40`_ worldwide. Tarantool services
Mail.Ru's hottest data, such as the session data of online users, the
properties of online applications, the caches of the underlying data, the
distribution and sharding algorithms, and much more. Outside Mail.Ru the
software is used by a growing number of projects in online gaming, digital
marketing, and social media industries. While product development is sponsored
by Mail.Ru, the roadmap, the bugs database and the development process are
fully open. The software incorporates patches from dozens of community
contributors. The Tarantool community writes and maintains most of the drivers
for programming languages.  The greater Lua community has hundreds of useful
packages most of which can be used in Tarantool applications with no change.


===============================================================================
                            Reporting bugs
===============================================================================

Please report bugs in Tarantool at http://github.com/tarantool/tarantool/issues.
You can contact developers directly on the `#tarantool`_ IRC channel on freenode,
or via a mailing list, `Tarantool Google group`_.

.. _#tarantool: irc://irc.freenode.net#tarantool
.. _Tarantool Google group: https://groups.google.com/forum/#!forum/tarantool
.. _Tarantool Russian-speaking list: https://googlegroups.com/group/tarantool-ru
.. _Tarantool Gitter chat: https://gitter.im/tarantool/tarantool

.. _BSD license: http://www.gnu.org/licenses/license-list.html#ModifiedBSD
.. _Mail.Ru: http://corp.mail.ru
.. _top 40: http://www.alexa.com/siteinfo/mail.ru
