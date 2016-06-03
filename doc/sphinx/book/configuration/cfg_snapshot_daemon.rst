    
    

    The snapshot daemon is a fiber which is constantly running.
    At intervals, it may make new snapshot (.snap) files and
    then may remove old snapshot files. If the snapshot daemon
    removes an old snapshot file, it will also remove any
    write-ahead log (.xlog) files that are older than the
    snapshot file and contain information that is present in
    the snapshot file.

    The :confval:`snapshot_period` and :confval:`snapshot_count`
    configuration settings determine how long the intervals are,
    and how many snapshots should exist before removals occur.

.. confval:: snapshot_period

    The interval between actions by the snapshot daemon, in seconds.
    If ``snapshot_period`` is set to a value greater than zero,
    and there is activity which causes change to a database,
    then the snapshot daemon will call :func:`box.snapshot` every
    ``snapshot_period`` seconds, creating a new snapshot file each time.

    For example: ``box.cfg{snapshot_period=3600}``
    will cause the snapshot daemon to create a new database snapshot
    once per hour.

    Type: integer |br|
    Default: 0 |br|
    Dynamic: yes |br|

.. confval:: snapshot_count

    The maximum number of snapshots that may exist on the snap_dir
    directory before the snapshot daemon will remove old snapshots.
    If snapshot_count equals zero, then the snapshot daemon does not remove
    old snapshots.
    For example:

    .. code-block:: lua

        box.cfg{
            snapshot_period = 3600,
            snapshot_count  = 10
        }

    will cause the snapshot daemon to create a new snapshot each hour until
    it has created ten snapshots. After that, it will remove the oldest
    snapshot (and any associated write-ahead-log files) after creating a new one.


    Type: integer |br|
    Default: 6 |br|
    Dynamic: yes |br|
