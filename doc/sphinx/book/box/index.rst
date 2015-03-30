-------------------------------------------------------------------------------
                              Database
-------------------------------------------------------------------------------

As well as executing Lua chunks or defining their own functions, users can exploit
the Tarantool server's storage functionality with the ``box`` Lua library.

=====================================================================
                     Packages of the box library
=====================================================================

The contents of the box library can be inspected at runtime with ``box``, with
no arguments. The packages inside the box library are:

.. toctree::
    :maxdepth: 1

    box_schema
    box_tuple
    box_space
    box_index
    box_session
    box_error
    box_introspection
    net_box
    admin
    atomic
    authentication
    limitations
    triggers

Every package contains one or more Lua functions. A few packages contain
members as well as functions. The functions allow data definition (create
alter drop), data manipulation (insert delete update select replace), and
introspection (inspecting contents of spaces, accessing server configuration).


.. container:: table

    **Complexity Factors that may affect data
    manipulation functions in the box library**

    +-------------------+-----------------------------------------------------+
    | Index size        | The number of index keys is the same as the number  |
    |                   | of tuples in the data set. For a TREE index, if     |
    |                   | there are more keys then the lookup time will be    |
    |                   | greater, although of course the effect is not       |
    |                   | linear. For a HASH index, if there are more keys    |
    |                   | then there is more RAM use, but the number of       |
    |                   | low-level steps tends to remain constant.           |
    +-------------------+-----------------------------------------------------+
    | Index type        | Typically a HASH index is faster than a TREE index  |
    |                   | if the number of tuples in the tuple set is greater |
    |                   | than one.                                           |
    +-------------------+-----------------------------------------------------+
    | Number of indexes | Ordinarily only one index is accessed to retrieve   |
    | accessed          | one tuple. But to update the tuple, there must be N |
    |                   | accesses if the tuple set has N different indexes.  |
    +-------------------+-----------------------------------------------------+
    | Number of tuples  | A few requests, for example select, can retrieve    |
    | accessed          | multiple tuples. This factor is usually less        |
    |                   | important than the others.                          |
    +-------------------+-----------------------------------------------------+
    | WAL settings      | The important setting for the write-ahead log is    |
    |                   | :ref:`wal_mode <wal_mode>`. If the setting causes   |
    |                   | no writing or                                       |
    |                   | delayed writing, this factor is unimportant. If the |
    |                   | settings causes every data-change request to wait   |
    |                   | for writing to finish on a slow device, this factor |
    |                   | is more important than all the others.              |
    +-------------------+-----------------------------------------------------+

In the discussion of each data-manipulation function there will be a note about
which Complexity Factors might affect the function's resource usage.

=====================================================================
            The two storage engines: memtx and sophia
=====================================================================

A storage engine is a set of very-low-level routines which actually store and
retrieve tuple values. Tarantool offers a choice of two storage engines: memtx
(the in-memory storage engine) and sophia (the on-disk storage engine).
To specify that the engine should be sophia, add a clause: ``engine = 'sophia'``.
The manual concentrates on memtx because it is the default and has been around
longer. But sophia is a working key-value engine and will especially appeal to
users who like to see data go directly to disk, so that recovery time might be
shorter and database size might be larger. For architectural explanations and
benchmarks, see sphia.org. On the other hand, sophia lacks some functions and
options that are available with memtx. Where that is the case, the relevant
description will contain the words "only applicable for the memtx storage engine".
