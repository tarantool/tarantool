----------------------------------------------------------------------------------------------------
                            Packages box.cfg, box.info, box.slab, and box.stat: server introspection
----------------------------------------------------------------------------------------------------

=====================================================================
                         Package `box.cfg`
=====================================================================

.. module:: box.cfg

The ``box.cfg`` package is for administrators to specify all the server
configuration parameters; the full description of the parameters is in
section :ref:`book-cfg`. Use ``box.cfg`` without braces to get read-only
access to those parameters.

.. data:: box.cfg

    EXAMPLE

    | :codenormal:`tarantool>` :codebold:`box.cfg`
    | :codenormal:`---`
    | :codenormal:`- too_long_threshold: 0.5`
    | :codenormal:`slab_alloc_factor: 1.1`
    | :codenormal:`slab_alloc_minimal: 64`
    | :codenormal:`background: false`
    | :codenormal:`slab_alloc_arena: 1`
    | :codenormal:`log_level: 5`
    | :codenormal:`...`
    | :codenormal:`...`

=====================================================================
                         Package `box.info`
=====================================================================

.. module:: box.info

The ``box.info`` package provides access to information about server variables
-- ``pid``, ``uptime``, ``version`` and others.

**recovery_lag** holds the difference (in seconds) between the current time on
the machine (wall clock time) and the time stamp of the last applied record.
In replication setup, this difference can indicate the delay taking place
before a change is applied to a replica.

**recovery_last_update** is the wall clock time of the last change recorded in
the write-ahead log. To convert it to human-readable time,
you can use **date -d@** 1306964594.980.

**status** is either "primary" or "replica/<hostname>".

.. function:: box.info()

    Since ``box.info`` contents are dynamic, it's not possible to iterate over
    keys with the Lua ``pairs()`` function. For this purpose, ``box.info()``
    builds and returns a Lua table with all keys and values provided in the
    package.

    :return: keys and values in the package.
    :rtype:  table

    EXAMPLE
    | :codenormal:`tarantool>` :codebold:`box.info()`
    | :codenormal:`---`
    | :codenormal:`- server:`
    | :codenormal:`lsn: 158`
    | :codenormal:`ro: false`
    | :codenormal:`uuid: 75967704-0115-47c2-9d03-bd2bdcc60d64`
    | :codenormal:`id: 1`
    | :codenormal:`pid: 32561`
    | :codenormal:`version: 1.6.4-411-gcff798b`
    | :codenormal:`snapshot_pid: 0`
    | :codenormal:`status: running`
    | :codenormal:`vclock: {1: 158}`
    | :codenormal:`replication:`
    | :codenormal:`status: off`
    | :codenormal:`uptime: 2778`
    | :codenormal:`...`

.. data:: status
          pid
          version
          ...

    .. code-block:: lua

    | :codenormal:`tarantool>` :codebold:`box.info.pid`
    | :codenormal:`---`
    | :codenormal:`- 1747`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.logger_pid`
    | :codenormal:`---`
    | :codenormal:`- 1748`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.version`
    | :codenormal:`---`
    | :codenormal:`- 1.6.4-411-gcff798b`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.uptime`
    | :codenormal:`---`
    | :codenormal:`- 3672`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.status`
    | :codenormal:`---`
    | :codenormal:`- running`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.recovery_lag`
    | :codenormal:`---`
    | :codenormal:`- 0.000`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.recovery_last_update`
    | :codenormal:`---`
    | :codenormal:`- 1306964594.980`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.info.snapshot_pid`
    | :codenormal:`---`
    | :codenormal:`- 0`
    | :codenormal:`...`

=====================================================================
                         Package `box.slab`
=====================================================================

.. module:: box.slab

The ``box.slab`` package provides access to slab allocator statistics. The
slab allocator is the main allocator used to store tuples. This can be used
to monitor the total memory use and memory fragmentation.

The display of slabs is broken down by the slab size -- 64-byte, 136-byte,
and so on. The example omits the slabs which are empty. The example display
is saying that: there are 16 items stored in the 64-byte slab (and 16*64=102
so bytes_used = 1024); there is 1 item stored in the 136-byte slab
(and 136*1=136 so bytes_used = 136); the arena_used value is the total of all
the bytes_used values (1024+136 = 1160); the arena_size value is the arena_used
value plus the total of all the bytes_free values (1160+4193200+4194088 = 8388448).
The arena_size and arena_used values are the amount of the % of
:confval:`slab_alloc_arena` that is already distributed to the slab allocator.

.. data:: slab

    .. code-block:: lua

    | :codenormal:`tarantool>` :codebold:`box.slab.info().arena_used`
    | :codenormal:`---`
    | :codenormal:`- 4194304`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.slab.info().arena_size`
    | :codenormal:`---`
    | :codenormal:`- 104857600`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.slab.info().slabs`
    | :codenormal:`---`
    | :codenormal:`- - {mem_free: 9320, mem_used: 6976, 'item_count': 109,`
    | :codenormal:`'item_size': 64, 'slab_count': 1, 'slab_size': 16384}`
    | :codenormal:`- {mem_free: 16224, mem_used: 72, 'item_count': 1,`
    | :codenormal:`'item_size': 72, 'slab_count': 1,'slab_size': 16384}`
    | :codenormal:`etc.`
    | :codenormal:`...`
    | :codenormal:`tarantool>` :codebold:`box.slab.info().slabs[1]`
    | :codenormal:`---`
    | :codenormal:`- {mem_free: 9320, mem_used: 6976, 'item_count': 109,`
    | :codenormal:`'item_size': 64, 'slab_count': 1, 'slab_size': 16384}`
    | :codenormal:`...`

=====================================================================
                         Package `box.stat`
=====================================================================

.. module:: box.stat

The ``box.stat`` package provides access to request statistics. Show the
average number of requests per second, and the total number of requests
since startup, broken down by request type.

.. data:: box.stat

        | :codenormal:`tarantool>` :codebold:`box.stat, type(box.stat) -- a virtual table`
        | :codenormal:`---`
        | :codenormal:`- []`
        | :codenormal:`- table`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.stat() -- the full contents of the table`
        | :codenormal:`---`
        | :codenormal:`- DELETE:`
        | :codenormal:`total: 48902544`
        | :codenormal:`rps: 147`
        | :codenormal:`EVAL:`
        | :codenormal:`total: 0`
        | :codenormal:`rps: 0`
        | :codenormal:`SELECT:`
        | :codenormal:`total: 388322317`
        | :codenormal:`rps: 1246`
        | :codenormal:`REPLACE:`
        | :codenormal:`total: 4`
        | :codenormal:`rps: 0`
        | :codenormal:`INSERT:`
        | :codenormal:`total: 48207694`
        | :codenormal:`rps: 139`
        | :codenormal:`AUTH:`
        | :codenormal:`total: 0`
        | :codenormal:`rps: 0`
        | :codenormal:`CALL:`
        | :codenormal:`total: 8`
        | :codenormal:`rps: 0`
        | :codenormal:`UPDATE:`
        | :codenormal:`total: 743350520`
        | :codenormal:`rps: 1874`
        | :codenormal:`...`
        | :codenormal:`tarantool>` :codebold:`box.stat().DELETE -- a selected item of the table`
        | :codenormal:`---`
        | :codenormal:`- total: 48902544`
        | :codenormal:`rps: 0`
        | :codenormal:`...`

