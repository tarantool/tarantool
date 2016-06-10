
.. confval:: background

    Run the server as a background task. The :ref:`logger <log-label>` and
    :ref:`pid_file <box-cfg-pid-file>` parameters must be non-null for this to work.

    Type: boolean |br|
    Default: false |br|
    Dynamic: no |br|

.. confval:: coredump

    Deprecated. Do not use.

    Type: boolean |br|
    Default: false |br|
    Dynamic: no |br|

.. confval:: custom_proc_title

    Add the given string to the server's :ref:`Process title <book-proctitle>`
    (what’s shown in the COMMAND column for :samp:`ps -ef` and :samp:`top -c` commands).

    For example, ordinarily :samp:`ps -ef` shows the Tarantool server process thus:

    .. code-block:: console

        $ ps -ef | grep tarantool
        1000     14939 14188  1 10:53 pts/2    00:00:13 tarantool <running>

    But if the configuration parameters include
    ``custom_proc_title='sessions'`` then the output looks like:

    .. code-block:: console

        $ ps -ef | grep tarantool
        1000     14939 14188  1 10:53 pts/2    00:00:16 tarantool <running>: sessions

    Type: string |br|
    Default: null |br|
    Dynamic: yes |br|

.. _box-cfg-listen:

.. confval:: listen

    The read/write data port number or :ref:`URI` (Universal Resource Identifier)
    string. Has no default value, so **must be specified** if connections will
    occur from remote clients that do not use “admin address” (the
    administrative host and port).

    A typical value is 3301. The listen parameter may also be set for
    :ref:`local hot standby <book_cfg_local_hot_standby>`.

    NOTE: A replica also binds to this port, and accepts connections, but these
    connections can only serve reads until the replica becomes a master.

    Type: integer or string |br|
    Default: null |br|
    Dynamic: yes |br|

.. _box-cfg-pid-file:

.. confval:: pid_file

    Store the process id in this file. Can be relative to :ref:`work_dir <box-cfg-work-dir>`.
    A typical value is “:file:`tarantool.pid`”.

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|

.. _box-cfg-read-only:

.. confval:: read_only

    Put the server in read-only mode. After this, any requests that try to change
    data will fail with error ER_READONLY.

    Type: boolean |br|
    Default: false |br|
    Dynamic: yes |br|

.. _box-cfg-snap-dir:

.. confval:: snap_dir

    A directory where snapshot (.snap) files will be stored. Can be relative to
    :ref:`work_dir <box-cfg-work-dir>`. If not specified, defaults to work_dir.
    See also :ref:`wal_dir <box-cfg-wal-dir>`.

    Type: string |br|
    Default: "." |br|
    Dynamic: no |br|

.. confval:: phia_dir

    A directory where phia files or sub-directories will be stored. Can be relative to
    :ref:`work_dir <box-cfg-work-dir>`. If not specified, defaults to work_dir.

    Type: string |br|
    Default: "." |br|
    Dynamic: no |br|

.. confval:: username

    UNIX user name to switch to after start.

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|

.. _box-cfg-wal-dir:

.. confval:: wal_dir

    A directory where write-ahead log (.xlog) files are stored. Can be
    relative to :ref:`work_dir <box-cfg-work-dir>`. Sometimes wal_dir
    and :ref:`snap_dir <box-cfg-snap-dir>` are specified with different values, so that
    write-ahead log files and snapshot files can be stored on different disks. If not
    specified, defaults to work_dir.

    Type: string |br|
    Default: "." |br|
    Dynamic: no |br|

.. _box-cfg-work-dir:

.. confval:: work_dir

    A directory where database working files will be stored. The server
    switches to work_dir with :manpage:`chdir(2)` after start. Can be
    relative to the current directory. If not specified, defaults to
    the current directory. Other directory parameters may be relative to work_dir,
    for example |br|
    :codenormal:`box.cfg{work_dir='/home/user/A',wal_dir='B',snap_dir='C'}` |br|
    will put xlog files in /home/user/A/B, snapshot files in /home/user/A/C,
    and all other files or sub-directories in /home/user/A.

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|
