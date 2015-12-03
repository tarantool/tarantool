-------------------------------------------------------------------------------
                            Administrative requests
-------------------------------------------------------------------------------

To learn which functions are considered to be administrative, type ``help()``.
A reference description also follows below:

.. function:: box.snapshot()

    Take a snapshot of all data and store it in :confval:`snap_dir`:samp:`/{<latest-lsn>}.snap`.
    To take a snapshot, Tarantool first enters the delayed garbage collection
    mode for all data. In this mode, tuples which were allocated before the
    snapshot has started are not freed until the snapshot has finished. To
    preserve consistency of the primary key, used to iterate over tuples, a
    copy-on-write technique is employed. If the master process changes part
    of a primary key, the corresponding process page is split, and the snapshot
    process obtains an old copy of the page. Since a snapshot is written
    sequentially, one can expect a very high write performance (averaging to
    80MB/second on modern disks), which means an average database instance gets
    saved in a matter of minutes. Note, that as long as there are any changes to
    the parent index memory through concurrent updates, there are going to be
    page splits, and therefore one needs to have some extra free memory to run
    this command. 10% of :confval:`slab_alloc_arena` is, on average, sufficient.
    This statement waits until a snapshot is taken and returns operation result.

    **Example:**

    .. code-block:: tarantoolsession

        tarantool> box.info.version
        ---
        - 1.6.8-66-g9093daa
        ...
        tarantool> box.snapshot()
        ---
        - ok
        ...
        tarantool> box.snapshot()
        ---
        - error: can't save snapshot, errno 17 (File exists)
        ...

    Taking a snapshot does not cause the server to start a new write-ahead log.
    Once a snapshot is taken, old WALs can be deleted as long as all replicas
    are up to date. But the WAL which was current at the time ``box.snapshot()``
    started must be kept for recovery, since it still contains log records
    written after the start of ``box.snapshot()``.

    An alternative way to save a snapshot is to send the server SIGUSR1 UNIX
    signal. While this approach could be handy, it is not recommended for use
    in automation: a signal provides no way to find out whether the snapshot
    was taken successfully or not.

.. function:: coredump()

    Fork and dump a core. Since Tarantool stores all tuples in memory, it can
    take some time. Mainly useful for debugging.
