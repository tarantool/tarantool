-------------------------------------------------------------------------------
                             Preface
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
packages most of which can become Tarantool extensions.

Users can create, modify and drop **Lua functions** at runtime.
Or they can define **Lua programs** that are loaded during startup for triggers,
background tasks, and interacting with networked peers. 
Unlike popular application development frameworks based on a "reactor" pattern,
networking in server-side Lua is sequential, yet very efficient, as it is built
on top of the **cooperative multitasking** environment that Tarantool itself
uses. A key feature is that the functions can access and modify databases
atomically.  Thus some developers look at it as a DBMS with a popular stored
procedure language, while others look at it as a replacement for multiple
components of multi-tier Web application architectures. Performance can be a few
hundred thousand transactions per second on a laptop, scalable upwards or outwards to
server farms.

**Tarantool is lock-free** in typical situations. Instead of the operating system's
concurrency primitives, such as mutexes, Tarantool uses cooperative multitasking to
handle thousands of connections simultaneously. There is a fixed number of
independent execution threads. The threads do not share state. Instead they
exchange data using low-overhead message queues. While this approach limits the
number of cores that the server will use, it removes competition for the memory
bus and ensures peak scalability of memory access and network throughput. CPU
utilization of a typical highly-loaded Tarantool server is under 10%.

Although Tarantool can run without it, the database management component
is a strong distinguishing feature.
(On the surface Tarantool is simply a Lua language interpreter and
the DBMS server is one of many built-in Lua packages.)
So here is a closer look at "The Box", or DBMS server. 

The database API allows for persisting Lua objects to disk,
managing object collections, creating or dropping secondary keys,
configuring and monitoring replication, performing controlled fail-over,
and executing Lua code triggered by database events. 
Remote database instances are accessible transparently via
a remote procedure invocation API.

Tarantool's DBMS server uses the **storage engine** concept, where
different sets of algorithms and data structures can be used for different
collections of objects. Two storage engines are built-in: an in-memory engine,
which all the data and indexes in RAM, and a two-level B-tree engine,
for data sets whose size is 10 to 1000 times the amount of available RAM.
All storage engines in Tarantool support transactions and
replication by using a common **write ahead log** (WAL). This ensures consistency
and crash safety of the persistent state.
The server performs inserts and updates atomically -- changes
are not considered complete until the WAL is written.
The logging subsystem supports group commit.

**Tarantool in-memory storage engine** (memtx) keeps all the data in
random-access memory, and therefore has very low read latency.
It also keeps persistent copies of the data in non-volatile storage,
such as disk, when users request "snapshots".
If a server stops and the random-access memory is lost,
then restarts, it reads the latest snapshot
and then replays the transactions that are in the log --
therefore no data is lost.

**Tarantool disk-based storage engine** is a fusion of ideas from modern filesystems, 
log-structured merge trees and classical B-trees. All data is organized
into **branches**. Each branch is represented by a file on disk. Branch 
size is a configuration option and normally is around 64MB. Each 
branch is a collection of pages, serving different purposes. Pages 
in a fully merged branch contain non-overlapping ranges of keys. A branch
can be partially merged if there were a lot of changes in its key range
recently. In that case some pages represent new keys and values in the
branch. The disk-based engine is append only: new data never overwrites
old data.

Unlike most NoSQL DBMSs, Tarantool supports **secondary index keys** as well as
primary keys, and **multi-part index keys**. The possible index types are HASH,
TREE, BITSET, and RTREE.

Tarantool supports **asynchronous replication**, locally or to remote hosts. 
The replication architecture can be **master-master**, that is, many nodes may
both handle the loads and receive what others have handled, for the same data
sets.

===============================================================================
                            Conventions
===============================================================================

This manual is written with `Sphinx`_ markup and uses
standard formatting conventions:

UNIX shell command input is prefixed with '``$``' and is in a fixed-width font:

.. code-block:: console

  $ tarantool --help

File names are also in a fixed-width font:

  :codenormal:`/path/to/var/dir` 

Text that represents user input is in boldface:

  :codebold:`$ your input here` 

Within user input, replaceable items are in italics:

  :codebold:`$ tarantool` :codebolditalic:`--option` 

===============================================================================
                            How to read the documentation
===============================================================================

To get started, one can either download the whole package
as described in the first part of Chapter 2 "Getting started",
or one can initially skip the download and connect to the online
Tarantool server running on the web at http://try.tarantool.org.
Either way, the first tryout can be a matter of following the example
in the second part of chapter 2: "Starting Tarantool and making your first database".

Chapter 3 "Databases" is about the Tarantool NoSQL DBMS.
If the only intent is to use Tarantool as a Lua application server,
most of the material in this chapter and in the following chapter
(Chapter 4 "Replication") will not be necessary.
Once again, the detailed instructions about each package can be regarded as reference material.

Chapter 6 "Server administration" and Chapter 5 "Configuration reference"
are primarily for administrators; however, every user should know something
about how the server is configured so the section about box.cfg is not skippable.
Chapter 7 "Connectors" is strictly for users who are connecting from a different
language such as C or Perl or Python -- other users will find no immediate need for this chapter.

The two long tutorials in Appendix C -- "Insert one million tuples with a Lua stored procedure"
and "Sum a JSON field for all tuples" -- start slowly and contain commentary that is especially
aimed at users who may not consider themselves experts at either Lua or NoSQL database management.

Finally, Appendix D "Plugins" has examples that will be essential for those users who want to
connect the Tarantool server to another DBMS: MySQL or PostgreSQL.

For experienced users, there is also a developer's guide and an extensive set of comments in the source code. 

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
.. _Sphinx: http://sphinx-doc.org/
