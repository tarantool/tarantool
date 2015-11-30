-------------------------------------------------------------------------------
   Server introspection
-------------------------------------------------------------------------------

=====================================================================
                         Package `box.cfg`
=====================================================================

.. module:: box.cfg

The ``box.cfg`` package is for administrators to specify all the server
configuration parameters; the full description of the parameters is in
section :ref:`book-cfg`. Use ``box.cfg`` without braces to get read-only
access to those parameters.

**Example:**

.. code-block:: tarantoolsession

    tarantool> box.cfg
    ---
    - snapshot_count: 6
      too_long_threshold: 0.5
      slab_alloc_factor: 1.1
      slab_alloc_maximal: 1048576
      background: false
      <...>
    ...

=====================================================================
                         Package `box.info`
=====================================================================

.. module:: box.info

The ``box.info`` package provides access to information about server variables.
Some important ones:

* **server.uuid** holds the unique identifier of the server. This value is also
  in the :data:`box.space._cluster` system space.
* **pid** is the process ID of the server. This value is also shown by the
  :ref:`tarantool <tarantool-build>` package.
* **version** is the Tarantool version. This value is also shown by
  :ref:`tarantool --version <tarantool-version>`.
* **uptime** is the number of seconds since the server started.

.. function:: box.info()

    Since ``box.info`` contents are dynamic, it's not possible to iterate over
    keys with the Lua ``pairs()`` function. For this purpose, ``box.info()``
    builds and returns a Lua table with all keys and values provided in the
    package.

    :return: keys and values in the package.
    :rtype:  table

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> box.info()
        ---
        - server:
            lsn: 158
            ro: false
            uuid: a2684219-b2b1-4334-88ab-50b0722283fd
            id: 1
          version: 1.6.8-66-g9093daa
          pid: 12932
          status: running
          vclock:
          - 158
          replication:
            status: off
          uptime: 908
        ...
        tarantool> box.info.pid
        ---
        - 12932
        ...
        tarantool> box.info.status
        ---
        - running
        ...
        tarantool> box.info.uptime
        ---
        - 1065
        ...
        tarantool> box.info.version
        ---
        - 1.6.8-66-g9093daa
        ...

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

**Example:**

.. code-block:: tarantoolsession

    tarantool> box.slab.info().arena_used
    ---
    - 4194304
    ...
    tarantool> box.slab.info().arena_size
    ---
    - 104857600
    ...
    tarantool> box.slab.stats()
    ---
    - - mem_free: 16248
        mem_used: 48
        item_count: 2
        item_size: 24
        slab_count: 1
        slab_size: 16384
      - mem_free: 15736
        mem_used: 560
        item_count: 14
        item_size: 40
        slab_count: 1
        slab_size: 16384
        <...>
    ...
    tarantool> box.slab.stats()[1]
    ---
    - mem_free: 15736
      mem_used: 560
      item_count: 14
      item_size: 40
      slab_count: 1
      slab_size: 16384
    ...

=====================================================================
                         Package `box.stat`
=====================================================================

The ``box.stat`` package provides access to request and network statistics.
Show the average number of requests per second, and the total number of
requests since startup, broken down by request type and network events statistics.

.. code-block:: tarantoolsession

    tarantool> type(box.stat), type(box.stat.net) -- a virtual tables
    ---
    - table
    - table
    ...
    tarantool> box.stat, box.stat.net
    ---
    - net: []
    - []
    ...
    tarantool> box.stat()
    ---
    - DELETE:
        total: 1873949
        rps: 123
      SELECT:
        total: 1237723
        rps: 4099
      INSERT:
        total: 0
        rps: 0
      EVAL:
        total: 0
        rps: 0
      CALL:
        total: 0
        rps: 0
      REPLACE:
        total: 1239123
        rps: 7849
      UPSERT:
        total: 0
        rps: 0
      AUTH:
        total: 0
        rps: 0
      ERROR:
        total: 0
        rps: 0
      UPDATE:
        total: 0
        rps: 0
    ...
    tarantool> box.stat().DELETE -- a selected item of the table
    ---
    - total: 0
      rps: 0
    ...
    tarantool> box.stat.net()
    ---
    - SENT:
        total: 0
        rps: 0
      EVENTS:
        total: 2
        rps: 0
      LOCKS:
        total: 6
        rps: 0
      RECEIVED:
        total: 0
        rps: 0
    ...
