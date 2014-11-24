-------------------------------------------------------------------------------
                                    Preface
-------------------------------------------------------------------------------

===========================================================
                  Tarantool: an overview
===========================================================

*Tarantool* is a Lua application server integrated with a database management system.
It has a "fiber" model which means that many applications can run simultaneously on a
single thread, while the Tarantool server can run multiple threads for input-output
and background maintenance. It integrates the LuaJIT -- "Just In Time" -- Lua compiler,
Lua libraries for most common applications, and the Tarantool Database Server which is
an established NoSQL DBMS. Thus it serves all the purposes that have made node.js and
Twisted popular in other environments, with the additional twist that it has a data
persistence level.

The code is free. The open-source license is
`BSD license <http://www.gnu.org/licenses/license-list.html#ModifiedBSD>`_.
The supported platforms are GNU/Linux, Mac OS and FreeBSD.

Tarantool's creator and biggest user is `Mail.Ru <https://en.wikipedia.org/wiki/Mail.ru>`_,
the largest internet company in Russia, with 30 million users, 25 million emails per day,
and a web site whose Alexa global rank is in the `top 40 <http://www.alexa.com/siteinfo/mail.ru>`_
worldwide. Tarantool services Mail.Ru's hottest data, such as the session data of online
users, the properties of online applications, the caches of the underlying data, the
distribution and sharding algorithms, and much more. Outside Mail.Ru the software is used
by a growing number of projects in online gaming, digital marketing, and social media
industries. While product development is sponsored by Mail.Ru, the roadmap, the bugs
database and the development process are fully open. The software incorporates patches
from dozens of community contributors. The Tarantool community writes and maintains most
of the drivers for programming languages. The greater Lua community has hundreds of
useful packages which can become Tarantool extensions.

Users can create, modify and drop **Lua functions** at runtime. Or they can define
**Lua programs** that are loaded during startup for triggers, background tasks, and
interacting with networked peers. Unlike popular application development frameworks
based on a "reactor" pattern, networking in server-side Lua is sequential, yet very
efficient, as it is built on top of the **cooperative multitasking** environment that
Tarantool itself uses. A key feature is that the functions can access and modify
databases atomically. Thus some developers look at it as a DBMS with a popular stored
procedure language, while others look at it as a replacement for multiple components
of multi-tier Web application architectures. Performance is a few thousand transactions
per second on a laptop, scalable upwards or outwards to server farms.

**Tarantool is lock-free**. Instead of the operating system's concurrency primitives,
such as mutexes, Tarantool uses cooperative multitasking to handle thousands of connections
simultaneously. There is a fixed number of independent execution threads. The threads do
not share state. Instead they exchange data using low-overhead message queues. While this
approach limits the number of cores that the server will use, it removes competition for
the memory bus and ensures peak scalability of memory access and network throughput. CPU
utilization of a typical highly-loaded Tarantool server is under 10%.

Although Tarantool can run without it, the database management component is a strong
distinguishing feature. So here is a closer look at "The Box", or DBMS server.

Ordinarily the server **keeps all the data in random-access memory**, and therefore has
very low read latency. The server **keeps persistent copies of the data in non-volatile
storage**, such as disk, when users request "snapshots". The server **maintains a
write-ahead log (WAL)** to ensure consistency and crash safety of the persistent copies.
The server **performs inserts and updates atomically** -- changes are not considered
complete until the WAL is written. The logging subsystem supports group commit.

When the rate of data changes is high, the write-ahead log file (or files) can grow
quickly. This uses up disk space, and increases the time necessary to restart the server
(because the server must start with the last snapshot, and then replay the transactions
that are in the log). The solution is to make snapshots frequently. Therefore the server
ensures that **snapshots are quick, resource-savvy, and non-blocking**. To accomplish
this, the server uses delayed garbage collection for data pages and uses a copy-on-write
technique for index pages. This ensures that the snapshot process has a consistent read view.

Unlike most NoSQL DBMSs, Tarantool supports **secondary index keys and multi-part index keys**
as well as primary keys. The possible index types are HASH, TREE, and BITSET.


Tarantool supports **asynchronous replication**, locally or to remote hosts. In this latest
version the replication architecture can be **master-master**, that is, many nodes may both
handle the loads and receive what others have handled, for the same data sets.

===========================================================
                      Conventions
===========================================================

This manual is written in `ReStructuredText <http://docutils.sourceforge.net/rst.html>`_
markup language

UNIX shell command input is prefixed with '$ ' and is in a fixed-width font:
:code:`$ tarantool --help`

File names are also in a fixed-width font: :file:`/path/to/var/dir`

Text that represents user input is in boldface: :samp:`$ {your input here}`

Within user input, replaceable items are in italics: :samp:`$ tarantool {--option}`

===========================================================
              How to read the documentation
===========================================================

To get started, one can either download the whole package as described in
the first part of Chapter 2 "Getting started", or one can initially skip
the download and connect to the online Tarantool server running on the web
at http://try.tarantool.org. Either way, the first tryout can be a matter
of following the example in the second part of chapter 2: "Starting Tarantool
and making your first database".

Chapter 3 "Lua and the Tarantool Lua packages" begins with explanations about
Lua and Tarantool's connection with Lua. Those explanations are necessary;
however, the detailed instructions about each package can be regarded as
reference material, skip over them lightly until their functionality is needed.

Chapter 4 "Databases" is about the Tarantool NoSQL DBMS. If the only intent is
to use Tarantool as a Lua application server, most of the material in this
chapter and in the following chapter (Chapter 5 "Replication") will not be
necessary. Once again, the detailed instructions about each package can be
regarded as reference material.

Chapter 6 "Server administration" and Chapter 7 "Configuration reference" are
primarily for administrators; however, every user should know something about
how the server is configured so the section about :code:`box.cfg` is not skippable.
Chapter 8 is strictly for users who are connecting from a different language such
as C or Perl or Python -- other users will find no immediate need for this chapter.

The two long tutorials in Appendix B -- "Insert one million tuples with a Lua
stored procedure" and "Sum a JSON field for all tuples" -- start slowly and
contain commentary that is especially aimed at users who may not consider
themselves experts at either Lua or NoSQL database management.

Finally, Appendix C "Plugins" has examples that will be essential for those
users who want to connect the Tarantool server to another DBMS: MySQL or PostgreSQL.

For experienced users, there is also a developer's guide and an extensive set of
comments in the source code.

===========================================================
                      Reporing Bugs
===========================================================

Please report bugs in Tarantool at http://github.com/tarantool/tarantool/issues/.
You can contact developers directly on the
`#tarantool <irc://irc.freenode.net#tarantool>`_ IRC channel on freenode, or via
a mailing list, `Tarantool Google group <https://googlegroups.com/group/tarantool>`_
