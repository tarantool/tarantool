    :confval:`snapshot_period`, |br|
    :confval:`snapshot_count` |br|

.. confval:: snapshot_period

    The interval between actions by the snapshot daemon, in seconds.
    The snapshot daemon is a fiber which is constantly running.
    If ``snapshot_period`` is set to a value greater than zero,
    then the snapshot daemon will call :func:`box.snapshot` every
    ``snapshot_period`` seconds, creating a new snapshot file each time.

    For example: ``box.cfg{snapshot_period=3600}``
    will cause the snapshot daemon to create a new database snapshot
    once per hour.

    Type: integer |br|
    Default: 0 |br|
    Dynamic: yes |br|

.. confval:: snapshot_count

    The maximum number of snapshots that the snapshot daemon maintains.
    For example:

    .. code-block:: lua

        box.cfg{
            snapshot_period = 3600
            snapshot_count  = 10
        }

    will cause the snapshot daemon to create a new snapshot each hour until
    it has created ten snapshots. After that, it will remove the oldest
    snapshot (and any associated write-ahead-log files) after creating a new one.
    If snapshot_count equals zero, then the snapshot daemon does not remove
    old snapshots.

    Type: integer |br|
    Default: 6 |br|
    Dynamic: yes |br|
