.. include:: ../directives.rst
.. highlight:: lua

-------------------------------------------------------------------------------
                        Configuration reference
-------------------------------------------------------------------------------

This chapter provides a reference of options which can be set on the command line or in an initialization file.

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
    the start of the milestone. The optional commit number and commit SHA1 are
    output for non-released versions only, and indicate how much this particular
    build has diverged from the last release.

    “Target” is the platform tarantool was built on. Some platform-specific details
    may follow this line.

    .. NOTE::

        Tarantool uses `git describe`_ to produce its version id, and this id
        can be used at any time to check out the corresponding source from our
        `git repository`_.

=====================================================================
                                URI
=====================================================================

Some configuration parameters and some functions depend on a URI, or
"Universal Resource Identifier". The URI string format is similar to the
`generic syntax for a URI schema`_. So it may contain (in order) a user name
for login, a password, a host name or host IP address, and a port number. Only
the port number is always mandatory. The password is mandatory if the user
name is specified, unless the user name is 'guest'. So, formally, the URI
syntax is ``[host:]port`` or ``[username:password@]host:port`` or if
``username='guest'`` it may be ``[username@]host:port``. If host is omitted,
then 'localhost' is assumed. If username:password is omitted, then 'guest'
is assumed. Some examples:

.. container:: table

    +-----------------------------+------------------------------+
    | URI fragment                | Example                      |
    +=============================+==============================+
    | port                        | 3301                         |
    +-----------------------------+------------------------------+
    | host:port                   | 127.0.0.1:3301               |
    +-----------------------------+------------------------------+
    | guest@host:port             | guest@mail.ru:3301           |
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

and suppose the command line is ``~/tarantool/src/tarantool script.lua ARG``.
Then the screen might look like this:

.. code-block:: lua

    $ export LISTEN_URI=3301
    $ ~/tarantool/src/tarantool script.lua ARG
    ... main/101/script.lua C> version 1.6.3-439-g7e1011b
    ... main/101/script.lua C> log level 5
    ... main/101/script.lua I> mapping 107374184 bytes for a shared arena...
    ... main/101/spawner C> initialized
    ... main/101/script.lua I> recovery start
    ... main/101/script.lua I> recovering from `./00000000000000000000.snap'
    ... main/101/script.lua I> primary: bound to 0.0.0.0:3301
    ... main/102/leave_local_hot_standby I> ready to accept requests
    Starting  ARG
    ... main C> entering the event loop

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
    | wal_dir           | string    | "."      |    no    | A directory where write-ahead log (.xlog) files |
    |                   |           |          |          | are stored. Can be relative to work_dir. Most   |
    |                   |           |          |          | commonly used so that snapshot files and        |
    |                   |           |          |          | write-ahead log files can be stored on separate |
    |                   |           |          |          | disks. If not specified, defaults to work_dir.  |
    +-------------------+-----------+----------+----------+-------------------------------------------------+
    | snap_dir          | string    | "."      |    no    | A directory where snapshot (.snap) files will   |
    |                   |           |          |          | be stored. Can be relative to work_dir. If not  |
    |                   |           |          |          | specified, defaults to work_dir. See also       |
    |                   |           |          |          | `wal_dir`_.                                     |
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
    |                   |           |          |          | `server process title`_ (what's shown in the    |
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

    +--------------------+-----------+----------+----------+-------------------------------------------------+
    | Name               | Type      | Default  | Dynamic? | Description                                     |
    +====================+===========+==========+==========+=================================================+
    | slab_alloc_arena   | float     | null     |    no    | How much memory Tarantool allocates to actually |
    |                    |           |          |          | store tuples, in gigabytes. When the limit is   |
    |                    |           |          |          | reached, INSERT or UPDATE requests begin        |
    |                    |           |          |          | failing with error `ER_MEMORY_ISSUE`_. While    |
    |                    |           |          |          | the server does not go beyond the defined limit |
    |                    |           |          |          | to allocate tuples, there is additional memory  |
    |                    |           |          |          | used to store indexes and connection            |
    |                    |           |          |          | information. Depending on actual configuration  |
    |                    |           |          |          | and workload, Tarantool can consume up to 20%   |
    |                    |           |          |          | more than the limit set here.                   |
    +--------------------+-----------+----------+----------+-------------------------------------------------+
    | slab_alloc_minimal | integer   | null     |    no    | Size of the smallest allocation unit. It can be |
    |                    |           |          |          | tuned down if most of the tuples are very small |
    +--------------------+-----------+----------+----------+-------------------------------------------------+
    | slab_alloc_factor  | float     | 2.0      |    no    | Use slab_alloc_factor as the multiplier for     |
    |                    |           |          |          | computing the sizes of memory chunks that       |
    |                    |           |          |          | tuples are stored in. A lower value may result  |
    |                    |           |          |          | in less wasted memory depending on the total    |
    |                    |           |          |          | amount of memory available and the distribution |
    |                    |           |          |          | of item sizes.                                  |
    +--------------------+-----------+----------+----------+-------------------------------------------------+
    | sophia             | table     | (see the |    no    | The default sophia configuration can be changed |
    |                    |           | note)    |          | with                                            |
    |                    |           |          |          |                                                 |
    |                    |           |          |          | .. code-block:: lua                             |
    |                    |           |          |          |                                                 |
    |                    |           |          |          |    sophia = {                                   |
    |                    |           |          |          |        page_size = number,                      |
    |                    |           |          |          |        threads = number,                        |
    |                    |           |          |          |        node_size = number,                      |
    |                    |           |          |          |        memory_limit = number                    |
    |                    |           |          |          |    }                                            |
    |                    |           |          |          |                                                 |
    |                    |           |          |          | This method may change in the future.           |
    +--------------------+-----------+----------+----------+-------------------------------------------------+

    **Snapshot daemon**

    +--------------------+-----------+----------+----------+-----------------------------------------------------+
    | Name               | Type      | Default  | Dynamic? | Description                                         |
    +====================+===========+==========+==========+=====================================================+
    | snapshot_period    | float     | 0        |   yes    | The interval between actions by the snapshot        |
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
    | snapshot_count     | float     | 6        |   yes    | The maximum number of snapshots that the            |
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
    | panic_on_wal_error   | boolean   | false    | no       | If there is an error while reading a write-ahead    |
    |                      |           |          |          | log file (at server start), abort.                  |
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
    |                      |           |          |          | `snap_dir`_ locations and moving snapshots to a     |
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
    |                      |           |          |          |

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
    |                      |           |          |          | The default user name is 'guest'. A replica server  |
    |                      |           |          |          | does not accept data-change                         |
    |                      |           |          |          | requests on the ``listen``                          |
    |                      |           |          |          | port. The replication_source parameter is dynamic,  |
    |                      |           |          |          | that is, to enter master mode, simply set           |
    |                      |           |          |          | replication_source to an empty string and issue     |
    |                      |           |          |          | "``box.cfg{replication_source=new-value}``"         |
    +----------------------+-----------+----------+----------+-----------------------------------------------------+

    **Networking**

    ho

    **Logging**

    

