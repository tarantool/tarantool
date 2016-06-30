    :ref:`panic_on_snap_error <cfg_binary_logging_snapshots-panic_on_snap_error>`, |br|
    :ref:`panic_on_wal_error <cfg_binary_logging_snapshots-panic_on_wal_error>`, |br|
    :ref:`rows_per_wal <cfg_binary_logging_snapshots-rows_per_wal>`, |br|
    :ref:`snap_io_rate_limit <cfg_binary_logging_snapshots-snap_io_rate_limit>`, |br|
    :ref:`wal_mode <cfg_binary_logging_snapshots-wal_mode>`, |br|
    :ref:`wal_dir_rescan_delay <cfg_binary_logging_snapshots-wal_dir_rescan_delay>` |br|

.. _cfg_binary_logging_snapshots-panic_on_snap_error:

.. confval:: panic_on_snap_error

    If there is an error while reading the snapshot file
    (at server start), abort.

    Type: boolean |br|
    Default: true |br|
    Dynamic: no |br|

.. _cfg_binary_logging_snapshots-panic_on_wal_error:

.. confval:: panic_on_wal_error

    If there is an error while reading a write-ahead log
    file (at server start or to relay to a replica), abort.

    Type: boolean |br|
    Default: true |br|
    Dynamic: yes |br|

.. _cfg_binary_logging_snapshots-rows_per_wal:

.. confval:: rows_per_wal

    How many log records to store in a single write-ahead log file.
    When this limit is reached, Tarantool creates another WAL file
    named :samp:`{<first-lsn-in-wal>}.xlog`. This can be useful for
    simple rsync-based backups.

    Type: integer |br|
    Default: 500000 |br|
    Dynamic: no |br|

.. _cfg_binary_logging_snapshots-snap_io_rate_limit:

.. confval:: snap_io_rate_limit

    Reduce the throttling effect of :ref:`box.snapshot <admin-snapshot>` on
    INSERT/UPDATE/DELETE performance by setting a limit on how many
    megabytes per second it can write to disk. The same can be
    achieved by splitting :ref:`wal_dir <cfg_basic-wal_dir>` and :ref:`snap_dir <cfg_basic-snap_dir>`
    locations and moving snapshots to a separate disk.

    Type: float |br|
    Default: null |br|
    Dynamic: **yes** |br|

.. _cfg_binary_logging_snapshots-wal_mode:

.. confval:: wal_mode

    Specify fiber-WAL-disk synchronization mode as:

    * ``none``: write-ahead log is not maintained;
    * ``write``: fibers wait for their data to be written to
      the write-ahead log (no :manpage:`fsync(2)`);
    * ``fsync``: fibers wait for their data, :manpage:`fsync(2)`
      follows each :manpage:`write(2)`;

    Type: string |br|
    Default: "write" |br|
    Dynamic: **yes** |br|

.. _cfg_binary_logging_snapshots-wal_dir_rescan_delay:

.. confval:: wal_dir_rescan_delay

    Number of seconds between periodic scans of the write-ahead-log
    file directory, when checking for changes to write-ahead-log
    files for the sake of replication or local hot standby.

    Type: float |br|
    Default: 2 |br|
    Dynamic: no |br|
