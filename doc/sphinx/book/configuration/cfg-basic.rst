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

    Inject the given string into :ref:`server process title <book-proctitle>`
    (what’s shown in the COMMAND column for :samp:`ps` and :samp:`top` commands).

    .. NOTE::

        For example, ordinarily ps shows the Tarantool server process thus:

            | :codenormal:`$` :codebold:`ps -ef | grep tarantool`
            | :codenormal:`1000     22364  2778  0 09:14 pts/0    00:00:00 tarantool: running`
            | :codenormal:`1000     22394 22364  0 09:14 pts/0    00:00:00 tarantool: spawner`
            | :codenormal:`tarantool: primary pri: 3301 adm: 3313`

        But if the configuration parameters include
        ``custom_proc_title='sessions'`` then the output looks like:

            | :codenormal:`$` :codebold:`ps -ef | grep tarantool`
            | :codenormal:`1000     22364  2778  0 09:14 pts/0    00:00:00 tarantool: running@sessions`
            | :codenormal:`1000     22394 22364  0 09:14 pts/0    00:00:00 tarantool: spawner@sessions`
            | :codenormal:`tarantool: primary pri: 3301 adm: 3313`

    Type: string |br|
    Default: null |br|
    Dynamic: no |br|

.. confval:: background

    Run the server as a background task. The :confval:`logger` and
    :confval:`pid_file` parameters must be non-null for this to work.

    Type: boolean |br|
    Default: false |br|
    Dynamic: no |br|
