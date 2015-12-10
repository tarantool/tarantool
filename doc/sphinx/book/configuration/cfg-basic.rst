    :confval:`username`, |br|
    :confval:`work_dir`, |br|
    :confval:`wal_dir`, |br|
    :confval:`snap_dir`, |br|
    :confval:`sophia_dir`, |br|
    :confval:`coredump`, |br|
    :confval:`listen`, |br|
    :confval:`coredump`, |br|
    :confval:`pid_file`, |br|
    :confval:`custom_proc_title`, |br|
    :confval:`background` |br|

.. confval:: username

    UNIX user name to switch to after start.

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|

.. confval:: work_dir

    A directory where database working files will be stored. The server
    switches to work_dir with :manpage:`chdir(2)` after start. Can be
    relative to the current directory. If not specified, defaults to
    the current directory.

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|

.. confval:: wal_dir

    A directory where write-ahead log (.xlog) files are stored. Can be
    relative to work_dir. Most commonly used so that snapshot files and
    write-ahead log files can be stored on separate disks. If not
    specified, defaults to work_dir.

    Type: string |br|
    Default: "." |br|
    Dynamic: no |br|

.. confval:: snap_dir

    A directory where snapshot (.snap) files will be stored. Can be relative to
    work_dir. If not specified, defaults to work_dir. See also :confval:`wal_dir`.

    Type: string |br|
    Default: "." |br|
    Dynamic: no |br|

.. confval:: sophia_dir

    A directory where sophia files will be stored. Can be relative to
    :confval:`work_dir`. If not specified, defaults to :file:`work_dir`.

    Type: string |br|
    Default: "sophia" |br|
    Dynamic: no |br|

.. confval:: coredump

    Deprecated. Do not use.

    Type: boolean |br|
    Default: false |br|
    Dynamic: no |br|

.. confval:: listen

    The read/write data port number or :ref:`URI` (Universal Resource Identifier)
    string. Has no default value, so **must be specified** if connections will
    occur from remote clients that do not use “admin address” (the
    administrative host and port).

    A typical value is 3301. The listen parameter may also be set for
    :ref:`local hot standby <book-cfg-local_hot_standy>`.

    .. NOTE::

        A replica also binds to this port, and accepts connections, but these
        connections can only serve reads until the replica becomes a master.

    Type: integer or string |br|
    Default: null |br|
    Dynamic: yes |br|

.. confval:: pid_file

    Store the process id in this file. Can be relative to :confval:`work_dir`.
    A typical value is “:file:`tarantool.pid`”.

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|

.. confval:: custom_proc_title

    Add the given string to the server's :ref:`Process title <book-proctitle>`
    (what’s shown in the COMMAND column for :samp:`ps -ef` and :samp:`top -c` commands).

    For example, ordinarily :samp:`ps` shows the Tarantool server process thus:

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

.. confval:: background

    Run the server as a background task. The :ref:`logger <log-label>` and
    :confval:`pid_file` parameters must be non-null for this to work.

    Type: boolean |br|
    Default: false |br|
    Dynamic: no |br|
