-------------------------------------------------------------------------------
                             Overview
-------------------------------------------------------------------------------

===============================================================================
          An Application Server Together With A Database Manager
===============================================================================


Tarantool is a Lua application server integrated with a database management system.
It has a "fiber" model which means that many Tarantool applications can run simultaneously on
a single thread, while the Tarantool server itself can run multiple threads for input-output
and background maintenance. It incorporates the LuaJIT -- "Just In Time" -- Lua compiler,
Lua libraries for most common applications, and the Tarantool Database Server which
is an established NoSQL DBMS. Thus Tarantool serves all the purposes that have made node.js
and Twisted popular, plus it supports data persistence.

The code is free. The open-source license is `BSD license`_. The supported platforms
are GNU/Linux, Mac OS and FreeBSD.

Tarantool's creator and biggest user is `Mail.Ru`_, the largest internet
company in Russia, with 30 million users, 25 million emails per day, and a web
site whose Alexa global rank is in the `top 40`_ worldwide. Tarantool services
Mail.Ru's hottest data, such as the session data of online users, the
properties of online applications, the caches of the underlying data, the
distribution and sharding algorithms, and much more. Outside Mail.Ru the
software is used by a growing number of projects in online gaming, digital
marketing, and social media industries. Although Mail.Ru is the sponsor
for product development, the roadmap and the bugs database and the development process are
fully open. The software incorporates patches from dozens of community
contributors. The Tarantool community writes and maintains most of the drivers
for programming languages.  The greater Lua community has hundreds of useful
packages most of which can become Tarantool extensions.

Users can create, modify and drop **Lua functions** at runtime.
Or they can define **Lua programs** that are loaded during startup for triggers,
background tasks, and interacting with networked peers. 
Unlike popular application development frameworks based on a "reactor" pattern,
networking in server-side Lua is sequential, yet very efficient, as it is built
on top of the **cooperative multitasking** environment that Tarantool itself
uses.

One of the built-in Lua packages is an API for the Database Management System.
Thus some developers see Tarantool as a DBMS with a popular stored
procedure language, while others see it as a Lua interpreter,
while still others see it as a replacement for many
components of multi-tier Web applications. Performance can be a few
hundred thousand transactions per second on a laptop, scalable upwards or outwards to
server farms.

===============================================================================
                                Database Features
===============================================================================

Tarantool can run without it, but "The Box" -- the DBMS server --
is a strong distinguishing feature.

The database API allows for permanently storing Lua objects,
managing object collections, creating or dropping secondary keys,
making changes atomically,
configuring and monitoring replication, performing controlled fail-over,
and executing Lua code triggered by database events.
Remote database instances are accessible transparently via
a remote-procedure-invocation API.

Tarantool's DBMS server uses the **storage engine** concept, where
different sets of algorithms and data structures can be used for different
situations. Two storage engines are built-in: an in-memory engine
which has all the data and indexes in RAM, and a two-level B-tree engine
for data sets whose size is 10 to 1000 times the amount of available RAM.
All storage engines in Tarantool support transactions and
replication by using a common **write ahead log** (WAL). This ensures consistency
and crash safety of the persistent state.
Changes are not considered complete until the WAL is written.
The logging subsystem supports group commit.

**Tarantool's in-memory storage engine** (memtx) keeps all the data in
random-access memory, and therefore has very low read latency.
It also keeps persistent copies of the data in non-volatile storage,
such as disk, when users request "snapshots".
If a server stops and the random-access memory is lost,
then restarts, it reads the latest snapshot
and then replays the transactions that are in the log --
therefore no data is lost.

**Tarantool's in-memory engine is lock-free** in typical situations. Instead of the operating system's
concurrency primitives, such as mutexes, Tarantool uses cooperative multitasking to
handle thousands of connections simultaneously. There is a fixed number of
independent execution threads. The threads do not share state. Instead they
exchange data using low-overhead message queues. While this approach limits the
number of cores that the server will use, it removes competition for the memory
bus and ensures peak scalability of memory access and network throughput. CPU
utilization of a typical highly-loaded Tarantool server is under 10%.
Searches are possible via **secondary index keys** as well as primary keys.

**Tarantool's disk-based storage engine** is a fusion of ideas from modern filesystems, 
log-structured merge trees and classical B-trees. All data is organized
into **branches**. Each branch is represented by a file on disk. Branch 
size is a configuration option and normally is around 64MB. Each 
branch is a collection of pages, serving different purposes. Pages 
in a fully merged branch contain non-overlapping ranges of keys. A branch
can be partially merged if there were a lot of changes in its key range
recently. In that case some pages represent new keys and values in the
branch. The disk-based storage engine is append only: new data never overwrites
old data.

Tarantool supports **multi-part index keys**. The possible index types are HASH,
TREE, BITSET, and RTREE.

Tarantool supports **asynchronous replication**, locally or to remote hosts. 
The replication architecture can be **master-master**, that is, many nodes may
both handle the loads and receive what others have handled, for the same data
sets.


.. _BSD license: http://www.gnu.org/licenses/license-list.html#ModifiedBSD
.. _Mail.Ru: http://api.mail.ru
.. _top 40: http://www.alexa.com/siteinfo/mail.ru
