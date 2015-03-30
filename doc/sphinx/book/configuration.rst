.. include:: ../directives.rst
.. highlight:: lua

-------------------------------------------------------------------------------
                        Configuration reference
-------------------------------------------------------------------------------

This chapter provides a reference of options which can be set on the command
line or in an initialization file.

Tarantool is started by entering the command:

.. program:: tarantool

.. code-block:: bash

    $ tarantool
    OR
    $ tarantool <options>
    OR
    $ tarantool <lua-initialization-file> [arguments]

=====================================================================
                        Command options
=====================================================================

.. option:: -h, --help

    Print an annotated list of all available options and exit.

.. option:: -V, --version

    Print product name and version, for example:

    .. code-block:: bash

        $  ./tarantool --version
        Tarantool 1.6.3-439-g7e1011b
        Target: Linux-x86_64-Debug
        ...

    In this example:
    “Tarantool” is the name of the reusable asynchronous networking
    programming framework.

    The 3-number version follows the standard ``<major>-<minor>-<patch>``
    scheme, in which ``<major>`` number is changed only rarely, ``<minor>`` is
    incremented for each new milestone and indicates possible incompatible
    changes, and ``<patch>`` stands for the number of bug fix releases made after
    the start of the milestone. For non-released versions only, there may be a
    commit number and commit SHA1 to indicate how much this particular build has
    diverged from the last release.

    “Target” is the platform tarantool was built on. Some platform-specific details
    may follow this line.

    .. NOTE::

        Tarantool uses `git describe`_ to produce its version id, and this id
        can be used at any time to check out the corresponding source from our
        `git repository`_.

.. _git describe: http://www.kernel.org/pub/software/scm/git/docs/git-describe.html
.. _git repository: http://github.com/tarantool/tarantool.git



=====================================================================
                                URI
=====================================================================

Some configuration parameters and some functions depend on a URI, or
"Universal Resource Identifier". The URI string format is similar to the
`generic syntax for a URI schema`_. So it may contain (in order) a user name
for login, a password, a host name or host IP address, and a port number. Only
the port number is always mandatory. The password is mandatory if the user
name is specified, unless the user name is 'guest'. So, formally, the URI
syntax is ``[host:]port`` or ``[username:password@]host:port``
If host is omitted, then 'localhost' is assumed.
If username:password is omitted, then 'guest' is assumed. Some examples:

.. _generic syntax for a URI schema: http://en.wikipedia.org/wiki/URI_scheme#Generic_syntax

.. container:: table

    +-----------------------------+------------------------------+
    | URI fragment                | Example                      |
    +=============================+==============================+
    | port                        | 3301                         |
    +-----------------------------+------------------------------+
    | host:port                   | 127.0.0.1:3301               |
    +-----------------------------+------------------------------+
    | username:password@host:port | notguest:sesame@mail.ru:3301 |
    +-----------------------------+------------------------------+

In certain circumstances a Unix socket may be used where a URI is required.

=====================================================================
                       Initialization file
=====================================================================

If the command to start Tarantool includes ``<lua-initialization-file>``, then
Tarantool begins by invoking the Lua program in the file, which by convention
may have the name "``script.lua``". The Lua program may get further arguments
from the command line or may use operating-system functions, such as ``getenv()``.
The Lua program almost always begins by invoking ``box.cfg()``, if the database
server will be used or if ports need to be opened. For example, suppose
``script.lua`` contains the lines

.. code-block:: lua

    #!/usr/bin/env tarantool
    box.cfg{
        listen              = os.getenv("LISTEN_URI"),
        slab_alloc_arena    = 0.1,
        pid_file            = "tarantool.pid",
        rows_per_wal        = 50
    }
    print('Starting ',arg[1])

and suppose the environment variable LISTEN_URI contains 3301,
and suppose the command line is ``~/tarantool/src/tarantool script.lua ARG``.
Then the screen might look like this:

.. code-block:: lua

    $ export LISTEN_URI=3301
    $ ~/tarantool/src/tarantool script.lua ARG
    ... main/101/script.lua C> version 1.6.3-439-g7e1011b
    ... main/101/script.lua C> log level 5
    ... main/101/script.lua I> mapping 107374184 bytes for a shared arena...
    ... main/101/script.lua I> recovery start
    ... main/101/script.lua I> recovering from `./00000000000000000000.snap'
    ... main/101/script.lua I> primary: bound to 0.0.0.0:3301
    ... main/102/leave_local_hot_standby I> ready to accept requests
    Starting  ARG
    ... main C> entering the event loop

.. _local_hot_standby:
.. _replication_port:
.. _slab_alloc_arena:
.. _replication_source:
.. _admin_port:
.. _snap_dir:
.. _wal_dir:
.. _wal_mode:
.. _snapshot daemon:
.. _logger:

=====================================================================
                Configuration parameters
=====================================================================

Configuration parameters have the form ``box.cfg{ key = value [, key = value ...]}``.
Since ``box.cfg`` may contain many configuration parameters and since some of the
parameters (such as directory addresses) are semi-permanent, it's best to keep
``box.cfg`` in a Lua file. Typically this Lua file is the initialization file
which is specified on the tarantool command line.

Most configuration parameters are for allocating resources, opening ports, and
specifying database behavior. All parameters are optional. A few parameters are
dynamic, that is, they can be changed at runtime by calling ``box.cfg{}``
a second time.

To see all the non-null parameters, say ``box.cfg`` (no parentheses). To see a
particular parameter, for example the listen address, say ``box.cfg.listen``.

The following tables describe all parameters for basic operation, for storage,
for binary logging and snapshots, for replication, for networking, and for logging.

.. container:: table

    **Basic parameters**

    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | Name              | Type      | Default  | Dynamic? | Description                                     |
    +===================+===========+==========+==========+=================================================+
    | username          | string    | null     |    no    | UNIX user name to switch to after start.        |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | work_dir          | string    | null     |    no    | A directory where database working files will   |
    |                   |           |          |          | be stored. The server switches to work_dir with |
    |                   |           |          |          | chdir(2) after start. Can be relative to the    |
    |                   |           |          |          | current directory. If not specified, defaults   |
    |                   |           |          |          | to the current directory.                       |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    |   wal_dir         | string    | "."      |    no    | A directory where write-ahead log (.xlog) files |
    |                   |           |          |          | are stored. Can be relative to work_dir. Most   |
    |                   |           |          |          | commonly used so that snapshot files and        |
    |                   |           |          |          | write-ahead log files can be stored on separate |
    |                   |           |          |          | disks. If not specified, defaults to work_dir.  |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | snap_dir          | string    | "."      |    no    | A directory where snapshot (.snap) files will   |
    |                   |           |          |          | be stored. Can be relative to work_dir. If not  |
    |                   |           |          |          | specified, defaults to work_dir. See also       |
    |                   |           |          |          | :ref:`wal_dir`.                                 |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | sophia_dir        | string    | "sophia" |    no    | A directory where sophia files will be stored.  |
    |                   |           |          |          | Can be relative to work_dir. If not specified,  |
    |                   |           |          |          | defaults to ``work_dir/sophia``.                |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | coredump          | boolean   | "false"  |    no    | Deprecated. Do not use.                         |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | listen            | integer   | "null"   |   yes    | The read/write data port number or `URI`_       |
    |                   | or string |          |          | (Universal Resource Identifier) string. Has no  |
    |                   |           |          |          | default value, so must be specified if          |
    |                   |           |          |          | connections will occur from remote clients that |
    |                   |           |          |          | do not use "admin address" (the administrative  |
    |                   |           |          |          | host and port). Note: a replica also binds to   |
    |                   |           |          |          | this port, and accepts connections, but these   |
    |                   |           |          |          | connections can only serve reads until the      |
    |                   |           |          |          | replica becomes a master. A typical value is    |
    |                   |           |          |          | 3301. The listen parameter may also be set      |
    |                   |           |          |          | for `local hot standby`_.                       |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | pid_file          | string    | "null"   |    no    | Store the process id in this file. Can be       |
    |                   |           |          |          | relative to work_dir. A typical value is        |
    |                   |           |          |          | "tarantool.pid".                                |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | custom_proc_title | string    | "null"   |    no    | Inject the given string into                    |
    |                   |           |          |          | :doc:`app_b_proctitle`  (what's shown in the    |
    |                   |           |          |          | COMMAND column for ps and top commands).        |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | background        | boolean   | false    |    no    | Run the server as a background task. The logger |
    |                   |           |          |          | and pid_file parameters must be non-null for    |
    |                   |           |          |          | this to work.                                   |
    |                   |           |          |          |                                                 |
    +-------------------+-----------+----------+----------+-------------------------------------------------+

    .. NOTE::

        **custom_proc_title**

            For example, ordinarily ps shows the Tarantool server process thus:

            .. code-block:: lua

                $ ps -ef | grep tarantool
                1000     22364  2778  0 09:14 pts/0    00:00:00 tarantool: running
                1000     22394 22364  0 09:14 pts/0    00:00:00 tarantool: spawner
                tarantool: primary pri: 3301 adm: 3313

            But if the configuration parameters include
            ``custom_proc_title='sessions'`` then the output looks like:

            .. code-block:: lua

                $ ps -ef | grep tarantool
                1000     22364  2778  0 09:14 pts/0    00:00:00 tarantool: running@sessions
                1000     22394 22364  0 09:14 pts/0    00:00:00 tarantool: spawner@sessions
                tarantool: primary pri: 3301 adm: 3313

    **Configuring the storage**

    +--------------------------+-----------+----------+----------+-------------------------------------------------+
    | Name                     | Type      | Default  | Dynamic? | Description                                     |
    +==========================+===========+==========+==========+=================================================+
    |   slab_alloc_arena       | float     | null     |    no    | How much memory Tarantool allocates to actually |
    |                          |           |          |          | store tuples, in gigabytes. When the limit is   |
    |                          |           |          |          | reached, INSERT or UPDATE requests begin        |
    |                          |           |          |          | failing with error :ref:`ER_MEMORY_ISSUE`. While|
    |                          |           |          |          | the server does not go beyond the defined limit |
    |                          |           |          |          | to allocate tuples, there is additional memory  |
    |                          |           |          |          | used to store indexes and connection            |
    |                          |           |          |          | information. Depending on actual configuration  |
    |                          |           |          |          | and workload, Tarantool can consume up to 20%   |
    |                          |           |          |          | more than the limit set here.                   |
    +--------------------------+-----------+----------+----------+-------------------------------------------------+
    | slab_alloc_minimal       | integer   | 64       |    no    | Size of the smallest allocation unit. It can be |
    |                          |           |          |          | tuned down if most of the tuples are very small |
    +--------------------------+-----------+----------+----------+-------------------------------------------------+
    | slab_alloc_maximal       | integer   | 1048576  |    no    | Size of the largest allocation unit. It can be  |
    |                          |           |          |          | tuned down up if it is necessary to store large |
    |                          |           |          |          | tuples.                                         |
    +--------------------------+-----------+----------+----------+-------------------------------------------------+
    | slab_alloc_factor        | float     | 2.0      |    no    | Use slab_alloc_factor as the multiplier for     |
    |                          |           |          |          | computing the sizes of memory chunks that       |
    |                          |           |          |          | tuples are stored in. A lower value may result  |
    |                          |           |          |          | in less wasted memory depending on the total    |
    |                          |           |          |          | amount of memory available and the distribution |
    |                          |           |          |          | of item sizes.                                  |
    +--------------------------+-----------+----------+----------+-------------------------------------------------+
    | sophia                   | table     | (see the |    no    | The default sophia configuration can be changed |
    |                          |           | note)    |          | with                                            |
    |                          |           |          |          |                                                 |
    |                          |           |          |          | .. code-block:: lua                             |
    |                          |           |          |          |                                                 |
    |                          |           |          |          |    sophia = {                                   |
    |                          |           |          |          |        page_size = number,                      |
    |                          |           |          |          |        threads = number,                        |
    |                          |           |          |          |        node_size = number,                      |
    |                          |           |          |          |        memory_limit = number                    |
    |                          |           |          |          |    }                                            |
    |                          |           |          |          |                                                 |
    |                          |           |          |          | This method may change in the future.           |
    +--------------------------+-----------+----------+----------+-------------------------------------------------+

    **Snapshot daemon**

    +--------------------+-----------+----------+----------+-----------------------------------------------------+
    | Name               | Type      | Default  | Dynamic? | Description                                         |
    +====================+===========+==========+==========+=====================================================+
    | snapshot_period    | integer   | 0        |   yes    | The interval between actions by the snapshot        |
    |                    |           |          |          | daemon, in seconds. The snapshot daemon is a        |
    |                    |           |          |          | fiber which is constantly running. If               |
    |                    |           |          |          | ``snapshot_period`` is set to a value greater       |
    |                    |           |          |          | than zero, then the snapshot daemon will call       |
    |                    |           |          |          | :func:`box.snapshot` every ``snapshot_period``      |
    |                    |           |          |          | seconds, creating a new snapshot file each          |
    |                    |           |          |          | time. For example,                                  |
    |                    |           |          |          | ``box.cfg{snapshot_period=3600}`` will cause        |
    |                    |           |          |          | the snapshot daemon to create a new database        |
    |                    |           |          |          | snapshot once per hour.                             |
    +--------------------+-----------+----------+----------+-----------------------------------------------------+
    | snapshot_count     | integer   | 6        |   yes    | The maximum number of snapshots that the            |
    |                    |           |          |          | snapshot daemon maintains. For example,             |
    |                    |           |          |          | ``box.cfg{snapshot_period=3600, snapshot_count=10}``|
    |                    |           |          |          | will cause the snapshot daemon to create a new      |
    |                    |           |          |          | snapshot each hour until it has created ten         |
    |                    |           |          |          | snapshots. After that, it will remove the           |
    |                    |           |          |          | oldest snapshot (and any associated                 |
    |                    |           |          |          | write-ahead-log files) after creating a new         |
    |                    |           |          |          | one. If ``snapshot_count`` equals zero, then the    |
    |                    |           |          |          | snapshot daemon does not remove old snapshots.      |
    +--------------------+-----------+----------+----------+-----------------------------------------------------+

    **Snapshot daemon**

    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | Name                 | Type      | Default  | Dynamic? | Description                                         |
    +======================+===========+==========+==========+=====================================================+
    | panic_on_snap_error  | boolean   | true     | no       | If there is an error while reading the snapshot     |
    |                      |           |          |          | file (at server start), abort.                      |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | panic_on_wal_error   | boolean   | true     | no       | If there is an error while reading a write-ahead    |
    |                      |           |          |          | log file (at server start or to relay to a replica),|
    |                      |           |          |          | abort.                                              |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | rows_per_val         | integer   | 500000   | no       | How many log records to store in a single           |
    |                      |           |          |          | write-ahead log file. When this limit is reached,   |
    |                      |           |          |          | Tarantool creates another WAL file named            |
    |                      |           |          |          | ``<first-lsn-in-wal>.xlog`` This can be useful for  |
    |                      |           |          |          | simple rsync-based backups.                         |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | snap_io_rate_limit   | float     | null     | **yes**  | Reduce the throttling effect of                     |
    |                      |           |          |          | :func:`box.snapshot()` on INSERT/UPDATE/DELETE      |
    |                      |           |          |          | performance by setting a limit on how many          |
    |                      |           |          |          | megabytes per second it can write to disk. The same |
    |                      |           |          |          | can be achieved by splitting `wal_dir`_ and         |
    |                      |           |          |          | :ref:`snap_dir` locations and moving snapshots to a |
    |                      |           |          |          | separate disk.                                      |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | wal_mode             | string    | "write"  | **yes**  | Specify fiber-WAL-disk synchronization mode as:     |
    |                      |           |          |          | ``none``: write-ahead log is not maintained;        |
    |                      |           |          |          | ``write``: fibers wait for their data to be written |
    |                      |           |          |          | to the write-ahead log (no fsync(2));               |
    |                      |           |          |          | ``fsync``: fibers wait for their data, fsync(2)     |
    |                      |           |          |          | follows each write(2);                              |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | wal_dir_rescan_delay | float     | 0.1      | no       | Number of seconds between periodic scans of the     |
    |                      |           |          |          | write-ahead-log file directory, when checking for   |
    |                      |           |          |          | changes to write-ahead-log files for the sake of    |
    |                      |           |          |          | replication or local hot standby.                   |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+

    **Replication**

    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | Name                 | Type      | Default  | Dynamic? | Description                                         |
    +======================+===========+==========+==========+=====================================================+
    | replication_source   | string    | null     | **yes**  | If replication_source is not an empty string, the   |
    |                      |           |          |          | server is considered to be a Tarantool replica. The |
    |                      |           |          |          | replica server will try to connect to the master    |
    |                      |           |          |          | which replication_source specifies with a `URI`_    |
    |                      |           |          |          | (Universal Resource Identifier), for example        |
    |                      |           |          |          | '``konstantin:secret_password@tarantool.org:3301``' |
    |                      |           |          |          | The default user name is 'guest'.                   |
    |                      |           |          |          | The replication_source parameter is dynamic,        |
    |                      |           |          |          | that is, to enter master mode, simply set           |
    |                      |           |          |          | replication_source to an empty string and issue     |
    |                      |           |          |          | "``box.cfg{replication_source=new-value}``"         |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+

    **Networking**

    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | Name                 | Type      | Default  | Dynamic? | Description                                         |
    +======================+===========+==========+==========+=====================================================+
    | io_collect_interval  | float     | null     | **yes**  | The server will sleep for io_collect_interval       |
    |                      |           |          |          | seconds between iterations of the event loop. Can   |
    |                      |           |          |          | be used to reduce CPU load in deployments in which  |
    |                      |           |          |          | the number of client connections is large, but      |
    |                      |           |          |          | requests are not so frequent (for example, each     |
    |                      |           |          |          | connection issues just a handful of requests per    |
    |                      |           |          |          | second).                                            |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | readahead            | integer   | 16320    | **yes**  | The size of the read-ahead buffer associated with a |
    |                      |           |          |          | client connection. The larger the buffer, the more  |
    |                      |           |          |          | memory an active connection consumes and the more   |
    |                      |           |          |          | requests can be read from the operating system      |
    |                      |           |          |          | buffer in a single system call. The rule of thumb   |
    |                      |           |          |          | is to make sure the buffer can contain at least a   |
    |                      |           |          |          | few dozen requests. Therefore, if a typical tuple   |
    |                      |           |          |          | in a request is large, e.g. a few kilobytes or even |
    |                      |           |          |          | megabytes, the read-ahead buffer size should be     |
    |                      |           |          |          | increased. If batched request processing is not     |
    |                      |           |          |          | used, it's prudent to leave this setting at its     |
    |                      |           |          |          | default.                                            |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+

    **Logging**

    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | Name                 | Type      | Default  | Dynamic? | Description                                         |
    +======================+===========+==========+==========+=====================================================+
    | log_level            | integer   | true     | **yes**  | How verbose the logging is. There are six log       |
    |                      |           |          |          | verbosity classes: 1 -- SYSERROR, 2 -- ERROR,       |
    |                      |           |          |          | 3 -- CRITICAL, 4 -- WARNING, 5 -- INFO, 6 -- DEBUG. |
    |                      |           |          |          | By setting log_level, one can enable logging of all |
    |                      |           |          |          | classes below or equal to the given level.          |
    |                      |           |          |          | Tarantool prints its logs to the standard error     |
    |                      |           |          |          | stream by default, but this can be changed with     |
    |                      |           |          |          | the "logger" configuration parameter.               |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | logger               | string    | "null"   | no       | By default, the log is sent to the standard error   |
    |                      |           |          |          | stream (``stderr``). If logger is specified, the    |
    |                      |           |          |          | log is sent to the file named in the string.        |
    |                      |           |          |          | Example setting: ``logger = 'tarantool.log'`` (this |
    |                      |           |          |          | will open tarantool.log for output on the server's  |
    |                      |           |          |          | default directory).                                 |
    |                      |           |          |          | If logger string begins with a pipe, for example    |
    |                      |           |          |          | '| cronolog tarantool.log', the program specified in|
    |                      |           |          |          | the option is executed at server start and all log  |
    |                      |           |          |          | When logging to a file, tarantool reopens the log   |
    |                      |           |          |          | on SIGHUP. When log is a program, it's pid is saved |
    |                      |           |          |          | in logger_pid variable of package log. You need to  |
    |                      |           |          |          | send it a signal to rotate logs.                    |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | logger_nonblock      | boolean   | true     | no       | If logger_nonblock equals true, Tarantool does not  |
    |                      |           |          |          | block on the log file descriptor when it's not      |
    |                      |           |          |          | ready for write, and drops the message instead. If  |
    |                      |           |          |          | log_level is high, and a lot of messages go to the  |
    |                      |           |          |          | log file, setting logger_nonblock to true may       |
    |                      |           |          |          | improve logging performance at the cost of some log |
    |                      |           |          |          | messages getting lost.                              |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+
    | too_long_threshold   | float     | 0.5      | **yes**  | If processing a request takes longer than the given |
    |                      |           |          |          | value (in seconds), warn about it in the log. Has   |
    |                      |           |          |          | effect only if log_level is less than or equal to   |
    |                      |           |          |          | 4 (WARNING).                                        |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+

=====================================================================
                         Local hot standby
=====================================================================

Local hot standby is a feature which provides a simple form of failover without
replication. To initiate it, start a second instance of the Tarantool server on
the same computer with the same :func:`box.cfg` configuration settings -
including the same directories and same non-null URIs. A warning should appear with a
message like

.. code-block:: lua

    W> primary: [URI] is already in use, will retry binding after [n] seconds

This is fine. It means that the second instance is ready to take over if the
first instance goes down.

The expectation is that there will be two instances of the server using the
same configuration. The first one to start will be the "primary" instance.
The second one to start will be the "standby" instance. The standby instance
will initialize and will try to connect on listen address,
but will fail because the primary instance has already taken it. So the
standby instance goes into a loop, reading the write ahead log which the
primary instance is writing (so the two instances are always in synch),
and trying to connect on the port. If the primary instance goes down for any
reason, the port will become free so the standby instance will succeed in
connecting, and will become the primary instance. Thus there is no noticeable
downtime if the primary instance goes down.

If this ``local_hot_standby`` feature is being used, then ``wal_mode`` should
not be equal to "none".
