:title: Overview
:slug: intro
:save_as: intro.html

===============================================================================
                             What is Tarantool?
===============================================================================

Tarantool is a NoSQL database running and a Lua application server. The code is
available for free under the terms of `BSD license`_. Supported platforms are
GNU/Linux, Mac OS and FreeBSD.

===============================================================================
                     An overview of the architecture
===============================================================================

The server **maintains all its data in random-access memory**, and therefore
has very low read latency. At the same time, a copy of the data is kept on
non-volatile storage (a disk drive), and inserts and updates are performed
atomically.

To ensure atomicity, consistency and crash-safety of the persistent copy, a
write-ahead log (WAL) is maintained, and each change is recorded in the WAL
before it is considered complete. The logging subsystem supports group commit.

If update and delete rate is high, a constantly growing write-ahead log file
(or files) can pose a disk space problem, and significantly increase time
necessary to restart from disk.  A simple solution is employed: the server
**can be requested to save a concise snapshot** of its current data. The
underlying operating system's **"copy-on-write"** feature is employed to take
the snapshot in a quick, resource-savvy and non-blocking manner. The
**"copy-on-write"** technique guarantees that snapshotting has minimal impact
on server performance.

**Tarantool is lock-free**. Instead of the operating system's concurrency
primitives, such as threads and mutexes, Tarantool uses a cooperative
multitasking environment to simultaneously operate on thousands of
connections. A fixed number of independent execution threads within
the server do not share state, but exchange data using low overhead
message queues. While this approach limits server scalability to a
few CPU cores, it removes competition for the memory bus and sets the
scalability limit to the top of memory and network throughput. CPU
utilization of a typical highly-loaded Tarantool server is under 10%.

===============================================================================
                             Key features
===============================================================================

Unlike most of NoSQL databases, Tarantool supports primary, **secondary keys,
multi-part keys**, HASH, TREE and BITSET index types.

Tarantool supports **Lua stored procedures**, which can access and modify data
atomically. Procedures can be created, modified and dropped at runtime.

Use of Lua as an extension language does not end with stored procedures: Lua
programs can be used during startup, to define triggers and background tasks,
interact with networked peers. Unlike popular application development
frameworks implemented around "reactor" pattern, networking in server-side Lua
is sequential, yet very efficient, as is built on top of the cooperating
multitasking environment used by the server itself.

Extended with Lua, Tarantool typically replaces more not one but a few existing
components with a single well-performing system, changing and simplifying
complex multi-tier Web application architectures.

Tarantool supports replication. Replicas may run locally or on a remote host.
Tarantool replication is asynchronous and does not block writes to the master.
When or if the master becomes unavailable, the replica can be switched to
assume the role of the master without server restart.

===============================================================================
                       How stable is the software?
===============================================================================

**The software is production-ready**. Tarantool has been created and is actively
used at `Mail.Ru`_, one of the leading Russian web content providers. At `Mail.Ru`_,
the software serves the **"hottest"** data, such as online users and their
sessions, online application properties, mapping between users and their
serving shards, and so on.

Outside `Mail.Ru`_ the software is used by a growing number of projects in online
gaming, digital marketing, social media industries. While product development
is sponsored by `Mail.Ru`_, the roadmap, bugs database and the development process
are fully open. The software incorporates patches from dozens of community
contributors, and most of the programming language drivers are written and
supported by the community.

.. _BSD license: http://www.gnu.org/licenses/license-list.html#ModifiedBSD
.. _Mail.Ru: http://api.mail.ru
