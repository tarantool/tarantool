.. _box-internals:

-------------------------------------------------------------------------------
                            Appendix B. Internals
-------------------------------------------------------------------------------

This section is for advanced users or users who wish to
know more about how the Tarantool program works. The
discussion here is technical but generalized.
The ultimate authority is the code.

========================================
Data persistence and the WAL file format
========================================

To maintain data persistence, Tarantool writes each data change request (INSERT,
UPDATE, DELETE, REPLACE) into a write-ahead log (WAL) file in the
:confval:`wal_dir <wal_dir>` directory. A new WAL file is created for every
:confval:`rows_per_wal <rows_per_wal>` records. Each data change request gets
assigned a continuously growing 64-bit log sequence number. The name of the WAL
file is based on the log sequence number of the first record in the file, plus
an extension ``.xlog``.

Apart from a log sequence number and the data change request (its format is the
same as in the binary protocol and is described in `doc/dev_guide/box-protocol.html`_),
each WAL record contains a header, some metadata, and then the data formatted
according to `msgpack`_ rules. For example this is what the WAL file looks like
after the first INSERT request ("s:insert({1})") for the introductory sandbox
exercise ":ref:`Starting Tarantool and making your first database <first database>` “.
On the left are the hexadecimal bytes that one would see with:

.. code-block:: console

    $ hexdump 00000000000000000000.xlog

and on the right are comments.

.. code-block:: none

   Hex dump of WAL file       Comment
   --------------------       -------
   58 4c 4f 47 0a             File header: "XLOG\n"
   30 2e 31 32 0a             File header: "0.12\n" = version
   ...                        (not shown = more header + tuples for system spaces)
   d5 ba 0b ab                Magic row marker always = 0xab0bbad5 if version 0.12
   19 00                      Length, not including length of header, = 25 bytes
   ce 16 a4 38 6f             Record header: previous crc32, current crc32,
   a7 cc 73 7f 00 00 66 39
   84                         msgpack code meaning "Map of 4 elements" follows
   00 02                         element#1: tag=request type, value=0x02=IPROTO_INSERT
   02 01                         element#2: tag=server id, value=0x01
   03 04                         element#3: tag=lsn, value=0x04
   04 cb 41 d4 e2 2f 62 fd d5 d4 element#4: tag=timestamp, value=an 8-byte "Double"
   82                         msgpack code meaning "map of 2 elements" follows
   10 cd 02 00                   element#1: tag=space id, value=512, big byte first
   21 91 01                      element#2: tag=tuple, value=1-element fixed array={1}

Tarantool processes requests atomically: a change is either accepted and recorded
in the WAL, or discarded completely. Let's clarify how this happens, using the
REPLACE request as an example:

1. The server attempts to locate the original tuple by primary key. If found, a
   reference to the tuple is retained for later use.

2. The new tuple is validated. If for example it does not contain an indexed
   field, or it has an indexed field whose type does not match the type
   according to the index definition, the change is aborted.

3. The new tuple replaces the old tuple in all existing indexes.

4. A message is sent to WAL writer running in a separate thread, requesting that
   the change be recorded in the WAL. The server switches to work on the next
   request until the write is acknowledged.

5. On success, a confirmation is sent to the client. On failure, a rollback
   procedure is begun. During the rollback procedure, the transaction processor
   rolls back all changes to the database which occurred after the first failed
   change, from latest to oldest, up to the first failed change. All rolled back
   requests are aborted with :errcode:`ER_WAL_IO <ER_WAL_IO>` error. No new
   change is applied while rollback is in progress. When the rollback procedure
   is finished, the server restarts the processing pipeline.

One advantage of the described algorithm is that complete request pipelining is
achieved, even for requests on the same value of the primary key. As a result,
database performance doesn't degrade even if all requests refer to the same
key in the same space.

The transaction processor thread communicates with the WAL writer thread using
asynchronous (yet reliable) messaging; the transaction processor thread, not
being blocked on WAL tasks, continues to handle requests quickly even at high
volumes of disk I/O. A response to a request is sent as soon as it is ready,
even if there were earlier incomplete requests on the same connection. In
particular, SELECT performance, even for SELECTs running on a connection packed
with UPDATEs and DELETEs, remains unaffected by disk load.

The WAL writer employs a number of durability modes, as defined in configuration
variable :confval:`wal_mode <wal_mode>`. It is possible to turn the write-ahead
log completely off, by setting :confval:`wal_mode <wal_mode>` to *none*. Even
without the write-ahead log it's still possible to take a persistent copy of the
entire data set with the :func:`box.snapshot() <box.snapshot()>` request.

An .xlog file always contains changes based on the primary key.
Even if the client requested an update or delete using
a secondary key, the record in the .xlog file will contain the primary key.

========================
The snapshot file format
========================

The format of a snapshot .snap file is nearly the same as the format of a WAL .xlog file.
However, the snapshot header differs: it contains the server's global unique identifier
and the snapshot file's position in history, relative to earlier snapshot files.
Also, the content differs: an .xlog file may contain records for any data-change
requests (inserts, updates, upserts, and deletes), a .snap file may only contain records
of inserts to memtx spaces.

Primarily, the .snap file's records are ordered by space id. Therefore the records of
system spaces, such as _schema and _space and _index and _func and _priv and _cluster,
will be at the start of the .snap file, before the records of any spaces
that were created by users.

Secondarily, the .snap file's records are ordered by primary key within space id.

====================
The Recovery Process
====================

The recovery process begins when box.cfg{} happens for the
first time after the Tarantool server starts.

The recovery process must recover the databases
as of the moment when the server was last shut down. For this it may
use the latest snapshot file and any WAL files that were written
after the snapshot. One complicating factor is that Tarantool
has two engines -- the memtx data must be reconstructed entirely
from the snapshot and the WAL files, while the phia data will
be on disk but might require updating around the time of a checkpoint.
(When a snapshot happens, Tarantool tells the phia engine to
make a checkpoint, and the snapshot operation is rolled back if
anything goes wrong, so phia's checkpoint is at least as fresh
as the snapshot file.)

Step 1
    Read the configuration parameters in the ``box.cfg{}`` request.
    Parameters which affect recovery may include :confval:`work_dir`,
    :confval:`wal_dir`, :confval:`snap_dir`, :confval:`phia_dir`,
    :confval:`panic_on_snap_error`, and :confval:`panic_on_wal_error`.

Step 2
    Find the latest snapshot file. Use its data to reconstruct the in-memory
    databases. Instruct the phia engine to recover to the latest checkpoint.

There are actually two variations of the reconstruction procedure for the memtx
databases, depending whether the recovery process is "default".

If it is default (``panic_on_snap_error`` is ``true`` and ``panic_on_wal_error``
is ``true``), memtx can read data in the snapshot with all indexes disabled.
First, all tuples are read into memory. Then, primary keys are built in bulk,
taking advantage of the fact that the data is already sorted by primary key
within each space.

If it is not default (``panic_on_snap_error`` is ``false`` or ``panic_on_wal_error``
is ``false``), Tarantool performs additional checking. Indexes are enabled at
the start, and tuples are added one by one. This means that any unique-key
constraint violations will be caught, and any duplicates will be skipped.
Normally there will be no constraint violations or duplicates, so these checks
are only made if an error has occurred.

Step 2
    Find the WAL file that was made at the time of, or after, the snapshot file.
    Read its log entries until the log-entry LSN is greater than the LSN of the
    snapshot, or greater than the LSN of the phia checkpoint. This is the
    recovery process's "start position"; it matches the current state of the engines.

Step 3
    Redo the log entries, from the start position to the end of the WAL. The
    engine skips a redo instruction if it is older than the engine's checkpoint.

Step 4
    For the memtx engine, re-create all secondary indexes.

.. _internals-replication:

===============================
Server Startup With Replication
===============================

In addition to the recovery process described above, the server must take
additional steps and precautions if :ref:`replication <box-replication>` is
enabled.

Once again the startup procedure is initiated by the ``box.cfg{}`` request.
One of the box.cfg parameters may be :confval:`replication_source`. We will
refer to this server, which is starting up due to box.cfg, as the "local" server
to distinguish it from the other servers in a cluster, which we will refer to as
"distant" servers.

*If there is no snapshot .snap file and replication_source is empty*: |br|
then the local server assumes it is an unreplicated "standalone" server, or is
the first server of a new replication cluster. It will generate new UUIDs for
itself and for the cluster. The server UUID is stored in the _cluster space; the
cluster UUID is stored in the _schema space. Since a snapshot contains all the
data in all the spaces, that means the local server's snapshot will contain the
server UUID and the cluster UUID. Therefore, when the local server restarts on
later occasions, it will be able to recover these UUIDs when it reads the .snap
file.

*If there is no snapshot .snap file and replication_source is not empty
and the _cluster space contains no other server UUIDs*: |br|
then the local server assumes it is not a standalone server, but is not yet part
of a cluster. It must now join the cluster. It will send its server UUID to the
first distant server which is listed in replication_source, which will act as a
master. This is called the "join request". When a distant server receives a join
request, it will send back:

(1) the distant server's cluster UUID,
(2) the contents of the distant server's .snap file. |br|
    When the local server receives this information, it puts the cluster UUID in
    its _schema space, puts the distant server's UUID and connection information
    in its _cluster space, and makes a snapshot containing all the data sent by
    the distant server. Then, if the local server has data in its WAL .xlog
    files, it sends that data to the distant server. The distant server will
    receive this and update its own copy of the data, and add the local server's
    UUID to its _cluster space.

*If there is no snapshot .snap file and replication_source is not empty
and the _cluster space contains other server UUIDs*: |br|
then the local server assumes it is not a standalone server, and is already part
of a cluster. It will send its server UUID and cluster UUID to all the distant
servers which are listed in replication_source. This is called the "on-connect
handshake". When a distant server receives an on-connect handshake: |br|

(1) the distant server compares its own copy of the cluster UUID to the one in
    the on-connect handshake. If there is no match, then the handshake fails and
    the local server will display an error.
(2) the distant server looks for a record of the connecting instance in its
    _cluster space. If there is none, then the handshake fails. |br|
    Otherwise the handshake is successful. The distant server will read any new
    information from its own .snap and .xlog files, and send the new requests to
    the local server.

In the end ... the local server knows what cluster it belongs to, the distant
server knows that the local server is a member of the cluster, and both servers
have the same database contents.

*If there is a snapshot file and replication source is not empty*: |br|
first the local server goes through the recovery process described in the
previous section, using its own .snap and .xlog files. Then it sends a
"subscribe" request to all the other servers of the cluster. The subscribe
request contains the server vector clock. The vector clock has a collection of
pairs 'server id, lsn' for every server in the _cluster system space. Each
distant server, upon receiving a subscribe request, will read its .xlog files'
requests and send them to the local server if (lsn of .xlog file request) is
greater than (lsn of the vector clock in the subscribe request). After all the
other servers of the cluster have responded to the local server's subscribe
request, the server startup is complete.

The following temporary limitations apply for version 1.6:

* The URIs in replication_source should all be in the same order on all servers.
  This is not mandatory but is an aid to consistency.
* The servers of a cluster should be started up at slightly different times.
  This is not mandatory but prevents a situation where each server is waiting
  for the other server to be ready.
* The maximum number of entries in the _cluster space is 32. Tuples for
  out-of-date replicas are not automatically re-used, so if this 32-replica
  limit is reached, users may have to reorganize the _cluster space manually.

.. _MsgPack: https://en.wikipedia.org/wiki/MessagePack
.. _doc/dev_guide/box-protocol.html: http://tarantool.org/doc/dev_guide/box-protocol.html
