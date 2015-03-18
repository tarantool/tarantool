.. include:: ../directives.rst
.. highlight:: lua

-------------------------------------------------------------------------------
                        Server administration
-------------------------------------------------------------------------------

Replication allows multiple Tarantool servers to work on copies of the same
databases. The databases are kept in synch because each server can communicate
its changes to all the other servers. Servers which share the same databases
are a "cluster". Each server in a cluster also has a numeric identifier which
is unique within the cluster, known as the "server id".

    To set up replication, it's necessary to set up the master servers which
    make the original data-change requests, set up the replica servers which
    copy data-change requests from masters, and establish procedures for
    recovery from a degraded state.

=====================================================================
                    Replication architecture
=====================================================================

A replica gets all updates from the master by continuously fetching and
applying its write-ahead log (WAL). Each record in the WAL represents a
single Tarantool data-change request such as INSERT or UPDATE or DELETE,
and is assigned a monotonically growing log sequence number (LSN). In
essence, Tarantool replication is row-based: each data change command is
fully deterministic and operates on a single tuple.

A stored program invocation is not written to the write-ahead log. Instead,
log events for actual data-change requests, performed by the Lua code, are
written to the log. This ensures that possible non-determinism of Lua does
not cause replication to go out of sync.

=====================================================================
                       Setting up the master
=====================================================================

To prepare the master for connections from the replica, it's only necessary
to include "listen" in the initial ``box.cfg`` request, for example
``box.cfg{listen=3301}``. A master with enabled "listen" URI can accept
connections from as many replicas as necessary on that URI. Each replica
has its own replication state.

=====================================================================
                        Setting up a replica
=====================================================================

A server requires a valid snapshot (.snap) file. A snapshot file is created
for a server the first time that ``box.cfg`` occurs for it. If this first
``box.cfg`` request occurs without a "replication_source" clause, then the
server is a master and starts its own new cluster with a new unique UUID.
If this first ``box.cfg`` request occurs with a "replication_source" clause,
then the server is a replica and its snapshot file, along with the cluster
information, is constructed from the write-ahead logs of the master.
Therefore, to start replication, specify `replication_source`_ in a ``box.cfg``
request. When a replica contacts a master for the first time, it becomes part
of a cluster. On subsequent occasions, it should always contact a master in
the same cluster.

Once connected to the master, the replica requests all changes that happened
after the latest local LSN. It is therefore necessary to keep WAL files on
the master host as long as there are replicas that haven't applied them yet.
A replica can be "re-seeded" by deleting all its files (the snapshot .snap
file and the WAL .xlog files), then starting replication again - the replica
will then catch up with the master by retrieving all the master's tuples.
Again, this procedure works only if the master's WAL files are present.

.. NOTE::

    Replication parameters are "dynamic", which allows the replica to become
    a master and vice versa with the help of the :func:`box.cfg` statement.

.. NOTE::

    The replica does not inherit the master's configuration parameters, such
    as the ones that cause the `snapshot daemon`_ to run on the master. To get
    the same behavior, one would have to set the relevant parameters explicitly
    so that they are the same on both master and replica.

=====================================================================
                Recovering from a degraded state
=====================================================================

"Degraded state" is a situation when the master becomes unavailable - due to
hardware or network failure, or due to a programming bug. There is no automatic
way for a replica to detect that the master is gone for good, since sources of
failure and replication environments vary significantly. So the detection of
degraded state requires a human inspection.

However, once a master failure is detected, the recovery is simple: declare
that the replica is now the new master, by saying ``box.cfg{... listen=URI}``.
Then, if there are updates on the old master that were not propagated before
the old master went down, they would have to be re-applied manually.



=====================================================================
  Instructions for quick startup of a new two-server simple cluster
=====================================================================

Step 1. Start the first server thus:

.. code-block:: lua

    box.cfg{listen=uri#1}
    -- replace with more restrictive request
    box.schema.user.grant('guest','read,write,execute','universe')
    box.snapshot()

... Now a new cluster exists.

Step 2. Check where the second server's files will go by looking at its
directories (`snap_dir`_ for snapshot files, `wal_dir`_ for .xlog files).
They must be empty - when the second server joins for the first time, it
has to be working with a clean slate so that the initial copy of the first
server's databases can happen without conflicts.

Step 3. Start the second server thus:

.. code-block:: lua

    box.cfg{listen=uri#2, replication_source=uri#1}

... where ``uri#1`` = the `URI`_ that the first server is listening on.

That's all.

In this configuration, the first server is the "master" and the second server
is the "replica". Henceforth every change that happens on the master will be
visible on the replica. A simple two-server cluster with the master on one
computer and the replica on a different computer is very common and provides
two benefits: FAILOVER (because if the master goes down then the replica can
take over), or LOAD BALANCING (because clients can connect to either the master
or the replica for select requests).

=====================================================================
                    Master-Master Replication
=====================================================================

In the simple master-replica configuration, the master's changes are seen by
the replica, but not vice versa, because the master was specified as the sole
replication source. Starting with Tarantool 1.6, it's possible to go both ways.
Starting with the simple configuration, the first server has to say:
``box.cfg{replication_source=uri#2}``. This request can be performed at any time.

In this configuration, both servers are "masters" and both servers are
"replicas". Henceforth every change that happens on either server will
be visible on the other. The failover benefit is still present, and the
load-balancing benefit is enhanced (because clients can connect to either
server for data-change requests as well as select requests).

If two operations for the same tuple take place "concurrently" (which can
involve a long interval because replication is asynchronous), and one of
the operations is ``delete`` or ``replace``, there is a possibility that
servers will end up with different contents.


=====================================================================
                All the "What If?" Questions
=====================================================================

:Q: What if there are more than two servers with master-master?
:A: On each server, specify the replication_source for all the others. For
    example, server #3 would have a request:
    ``box.cfg{replication_source=uri#1, replication_source=uri#2}``.

:Q: What if a a server should be taken out of the cluster?
:A: Run ``box.cfg{}`` again specifying a blank replication source:
    ``box.cfg{replication_source=''}``.

:Q: What if a server leaves the cluster?
:A: The other servers carry on. If the wayward server rejoins, it will receive
    all the updates that the other servers made while it was away.

:Q: What if two servers both change the same tuple?
:A: The last changer wins. For example, suppose that server#1 changes the tuple,
    then server#2 changes the tuple. In that case server#2's change overrides
    whatever server#1 did. In order to keep track of who came last, Tarantool
    implements a `vector clock`_.

:Q: What if a master disappears and the replica must take over?
:A: A message will appear on the replica stating that the connection is lost.
    The replica must now become independent, which can be done by saying
    ``box.cfg{replication_source=''}``.

:Q: What if it's necessary to know what cluster a server is in?
:A: The identification of the cluster is a UUID which is generated when the
    first master starts for the first time. This UUID is stored in a tuple
    of the :data:`box.space._cluster` system space, and in a tuple of the
    :data:`box.space._schema` system space. So to see it, say:
    ``box.space._schema:select{'cluster'}``

:Q: What if one of the server's files is corrupted or deleted?
:A: Stop the server, destroy all the database files (the ones with extension
    "snap" or "xlog" or ".inprogress"), restart the server, and catch up with
    the master by contacting it again (just say ``box.cfg{...replication_source=...}``).

:Q: What if replication causes security concerns?
:A: Prevent unauthorized replication sources by associating a password with
    every user that has access privileges for the relevant spaces. That way,
    the `URI`_ for the replication_source parameter will always have to have
    the long form ``replication_source='username:password@host:port'``.

.. _vector clock: https://en.wikipedia.org/wiki/Vector_clock

=====================================================================
                    Hands-On Replication Tutorial
=====================================================================

After following the steps here, an administrator will have experience creating
a cluster and adding a replica.

Start two shells. Put them side by side on the screen.

+-------------------------------------------------------------------------------+-------------------------------------------------------------------------------+
|                                   Terminal #1                                 |                                   Terminal #2                                 |
+===============================================================================+===============================================================================+
|                                                                               |                                                                               |
| .. code-block:: lua                                                           | .. code-block:: lua                                                           |
|                                                                               |                                                                               |
|     $                                                                         |     $                                                                         |
|                                                                               |                                                                               |
+-------------------------------------------------------------------------------+-------------------------------------------------------------------------------+

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
