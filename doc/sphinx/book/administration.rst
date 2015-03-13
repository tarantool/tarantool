.. include:: ../directives.rst
.. highlight:: lua

-------------------------------------------------------------------------------
                        Server administration
-------------------------------------------------------------------------------

Typical server administration tasks include starting and stopping the server,
reloading configuration, taking snapshots, log rotation.

=====================================================================
                        Server signal handling
=====================================================================

The server is configured to shut down gracefully on SIGTERM and SIGINT (keyboard
interrupt) or SIGHUP. SIGUSR1 can be used to save a snapshot. All other signals
are blocked or ignored. The signals are processed in the main event loop. Thus,
if the control flow never reaches the event loop (thanks to a runaway stored
procedure), the server stops responding to any signal, and can only be killed
with SIGKILL (this signal can not be ignored).


=====================================================================
                        Utility ``tarantool``
=====================================================================

.. program:: tarantool

If ``tarantool`` is started without an initialization file, then there will be
a prompt ("``tarantool>``") and it will be possible to enter requests. When
used this way, ``tarantool`` is a client program as well as a server program.

This section shows all legal syntax for the tarantool program, with short notes
and examples. Other client programs may have similar options and request
syntaxes. Some of the information in this section is duplicated in the
`Configuration Reference`_ chapter.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            Conventions used in this section
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Tokens are character sequences which are treated as syntactic units within
requests. Square brackets [ and ] enclose optional syntax. Three dots in a
row ... mean the preceding tokens may be repeated. A vertical bar | means
the preceding and following tokens are mutually exclusive alternatives.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Options when starting client from the command line
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

General form:

.. code-block:: bash

    $ tarantool
    OR
    $ tarantool <options>
    OR
    $ tarantool <lua-initialization-file> [arguments]

<lua-initialization-file> can be any script containing code for initializing.
Effect: The code in the file is executed during startup. Example: ``init.lua``.
Notes: If a script is used, there will be no prompt. The script should contain
configuration information including "``box.cfg{...listen=...}``" or
"``box.listen(...)``" so that a separate program can connect to the server via
one of the ports.

Option is one of the following (in alphabetical order by the long form of the
option):

.. option:: -?, -h, --help

    Client displays a help message including a list of options.

    .. code-block:: bash

        tarantool --help

    The program stops after displaying the help.

.. option:: -V, --version

    .. code-block:: bash

        tarantool --version

    The program stops after displaying the version.


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      Tokens, requests, and special key combinations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Procedure identifiers are: Any sequence of letters, digits, or underscores
which is legal according to the rules for Lua identifiers. Procedure
identifiers are also called function names. Notes: function names are case
insensitive so ``insert`` and ``Insert`` are not the same thing.

tring literals are: Any sequence of zero or more characters enclosed in
single quotes. Double quotes are legal but single quotes are preferred.
Enclosing in double square brackets is good for multi-line strings as
described in `Lua documentation`_.

Example:

.. code-block:: lua

    'Hello, world', 'A', [[A\B!]].

Numeric literals are: Character sequences containing only digits, optionally
preceded by + or -. Examples: 55, -. Notes: Tarantool NUM data type is
unsigned, so -1 is understood as a large unsigned number.

Single-byte tokens are: * or , or ( or ). Examples: * , ( ).

Tokens must be separated from each other by one or more spaces, except that
spaces are not necessary around single-byte tokens or string literals.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                        Requests
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generally requests are entered following the prompt in interactive mode while
``tarantool`` is running. (A prompt will be the word tarantool and a
greater-than sign, for example ``tarantool>``). The end-of-request marker is by
default a newline (line feed).

For multi-line requests, it is possible to change the end-of-request marker.
Syntax: ``console = require('console'); console.delimiter(string-literal)``.
The string-literal must be a value in single quotes. Effect: string becomes
end-of-request delimiter, so newline alone is not treated as end of request.
To go back to normal mode: ``console.delimiter('')string-literal``. Example:

.. code-block:: lua

    console = require('console'); console.delimiter('!')
    function f ()
      statement_1 = 'a'
      statement_2 = 'b'
    end!
    console.delimiter('')!

For a condensed Backus-Naur Form [BNF] description of the suggested form of
client requests, see `doc/box-protocol.html`_ and `doc/sql.txt`_.

In *interactive* mode, one types requests and gets results. Typically the
requests are typed in by the user following prompts. Here is an example of an interactive-mode tarantool client session:

.. code-block:: bash

    $ tarantool
                    [ tarantool will display an introductory message
                      including version number here ]
    tarantool> box.cfg{listen=3301}
                    [ tarantool will display configuration information
                      here ]
    tarantool> s = box.schema.space.create('tester')
                    [ tarantool may display an in-progress message here ]
    ---
    ...
    tarantool> s:create_index('primary', {type = 'hash', parts = {1, 'NUM'}})
    ---
    ...
    tarantool> box.space.tester:insert{1,'My first tuple'}
    ---
    - [1, 'My first tuple']
    ...
    tarantool> box.space.tester:select(1)
    ---
    - - [1, 'My first tuple']
    ...
    tarantool> box.space.tester:drop()
    ---
    ...
    tarantool> os.exit()
    2014-04-30 10:28:00.886 [20436] main/101/spawner I> Exiting: master shutdown
    $

Explanatory notes about what tarantool displayed in the above example:

* Many requests return typed objects. In the case of "``box.cfg{listen=3301}``",
  this result is displayed on the screen. If the request had assigned the result
  to a variable, for example "``c = box.cfg{listen=3301}``", then the result
  would not have been displayed on the screen.
* A display of an object always begins with "``---``" and ends with "``...``".
* The insert request returns an object of type = tuple, so the object display line begins with a single dash ('``-``'). However, the select request returns an object of type = table of tuples, so the object display line begins with two dashes ('``- -``').

=====================================================================
                        Utility ``tarantoolctl``
=====================================================================

.. program:: tarantoolctl

With ``tarantoolctl`` one can say: "start an instance of the Tarantool server
which runs a single user-written Lua program, allocating disk resources
specifically for that program, via a standardized deployment method."
If Tarantool was downloaded from source, then the script is in
:file:`~/extra/dist/tarantoolctl`. If Tarantool was installed with Debian or
Red Hat installation packages, the script is renamed :program:`tarantoolctl`
and is in :file:`/usr/bin/tarantoolctl`. The script handles such things as:
starting, stopping, rotating logs, logging in to the application's console,
and checking status.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            configuring for tarantoolctl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The :program:`tarantoolctl` script will read a configuration file named
:file:`/etc/sysconfig/tarantool`, or :file:`/etc/default/tarantool`. Most
of the settings are similar to the settings used by ``box.cfg{...};``
however, tarantoolctl adjusts some of them by adding an application name.
A copy of :file:`/etc/sysconfig/tarantool`, with defaults for all settings,
would look like this:

.. code-block:: lua

    default_cfg = {
        pid_file   = "/var/run/tarantool",
        wal_dir    = "/var/lib/tarantool",
        snap_dir   = "/var/lib/tarantool",
        sophia_dir = "/var/lib/tarantool",
        logger     = "/var/log/tarantool",
        username   = "tarantool",
    }
    instance_dir = "/etc/tarantool/instances.enabled"

The settings in the above script are:

``pid_file``
    The directory for the pid file and control-socket file. The
    script will add ":file:`/instance-name`" to the directory name.

``wal_dir``
    The directory for the write-ahead :path:`*.xlog` files. The
    script will add ":file:`/instance-name`" to the directory-name.

``snap_dir``
    The directory for the snapshot :path:`*.snap` files. The script
    will add ":file:`/instance-name`" to the directory-name.

``sophia_dir``
    The directory for the sophia-storage-engine files. The script
    will add ":file:`/sophia/instance-name`" to the directory-name.

``logger``
    The place where the application log will go. The script will
    add ":file:`/instance-name.log`" to the name.

``username``
    the user that runs the tarantool server. This is the operating-system
    user name rather than the Tarantool-client user name.

``instance_dir``
    the directory where all applications for this host are stored. The user
    who writes an application for :program:`tarantoolctl` must put the
    application's source code in this directory, or a symbolic link. For
    examples in this section the application name my_app will be used, and
    its source will have to be in :file:`instance_dir/my_app.lua`.


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            commands for tarantoolctl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The command format is ``tarantoolctl operation application-name``, where
operation is one of: start, stop, status, logrotate, enter. Thus ...


