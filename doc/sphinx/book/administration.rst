-------------------------------------------------------------------------------
                        Server administration
-------------------------------------------------------------------------------

Typical server administration tasks include starting and stopping the server,
reloading configuration, taking snapshots, log rotation.

=====================================================================
                        Server signal handling
=====================================================================

The server processes these signals during the main thread event loop:

.. glossary::

    SIGHUP
        may cause log file rotation, see
        :ref:`the example in section "Logging" <logging_example>`.

    SIGUSR1
        may cause saving of a snapshot, see the description of
        :func:`box.snapshot`.

    SIGTERM
        may cause graceful shutdown (information will be saved first).

    SIGINT
        (also known as keyboard interrupt) may cause graceful shutdown.

    SIGKILL
        causes shutdown.

Other signals will result in behavior defined by the operating system. Signals
other than SIGKILL may be ignored, especially if the server is executing a
long-running procedure which prevents return to the main thread event loop.

.. _book-proctitle:

=====================================================================
                        Process title
=====================================================================

Linux and FreeBSD operating systems allow a running process to modify its title,
which otherwise contains the program name. Tarantool uses this feature to help
meet the needs of system administration, such as figuring out what services are
running on a host, their status, and so on.

A Tarantool server's process title has these components:

.. cssclass:: highlight
.. parsed-literal::

    **program_name** [**initialization_file_name**] **<role_name>** [**custom_proc_title**]

* **program_name** is typically "tarantool".
* **initialization_file_name** is the name of an :ref:`initialization file <init-label>` if one was specified.
* **role_name** is:
  - "running" (ordinary node "ready to accept requests"),
  - "loading" (ordinary node recovering from old snap and wal files),
  - "orphan" (not in a cluster),
  - "hot_standby" (see section :ref:`local hot standby <book-cfg-local_hot_standy>`), or
  - "dumper" + process-id (saving a snapshot).
* **custom_proc_title** is taken from the :confval:`custom_proc_title` configuration parameter, if one was specified.

For example:

.. code-block:: console

    $ ps -AF | grep tarantool
    1000     17337 16716  1 91362  6916   0 11:07 pts/5    00:00:13 tarantool script.lua <running>


.. _using-tarantool-as-a-client:

=====================================================================
                        Using ``tarantool`` as a client
=====================================================================

.. program:: tarantool

If ``tarantool`` is started without an :ref:`initialization file <init-label>`,
or if the initialization file contains :func:`console.start()`, then ``tarantool``
enters interactive mode. There will be a prompt ("``tarantool>``") and it will
be possible to enter requests. When used this way, ``tarantool`` can be
a client for a remote server.

This section shows all legal syntax for the tarantool program, with short notes
and examples. Other client programs may have similar options and request
syntaxes. Some of the information in this section is duplicated in the
:ref:`book-cfg` chapter.

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

.. cssclass:: highlight
.. parsed-literal::

    $ **tarantool**
    OR
    $ **tarantool** *options*
    OR
    $ **tarantool** *lua-initialization-file* **[** *arguments* **]**

*lua-initialization-file* can be any script containing code for initializing.
Effect: The code in the file is executed during startup. Example: ``init.lua``.
Notes: If a script is used, there will be no prompt. The script should contain
configuration information including ``box.cfg{...listen=...}`` or
``box.listen(...)`` so that a separate program can connect to the server via
one of the ports.

Option is one of the following (in alphabetical order by the long form of the
option):

.. option:: -?, -h, --help

    Client displays a help message including a list of options.
    Example: ``tarantool --help``.
    The program stops after displaying the help.

.. option:: -V, --version

    Client displays version information.
    Example: ``tarantool --version``.
    The program stops after displaying the version.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      Tokens, requests, and special key combinations
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Procedure identifiers are: Any sequence of letters, digits, or underscores
which is legal according to the rules for Lua identifiers. Procedure
identifiers are also called function names. Notes: function names are case
sensitive so ``insert`` and ``Insert`` are not the same thing.

String literals are: Any sequence of zero or more characters enclosed in
single quotes. Double quotes are legal but single quotes are preferred.
Enclosing in double square brackets is good for multi-line strings as
described in `Lua documentation`_. Examples: 'Hello, world', 'A', [[A\\B!]].

.. _Lua documentation: http://www.lua.org/pil/2.4.html

Numeric literals are: Character sequences containing only digits, optionally
preceded by + or -. Large or floating-point numeric
literals may include decimal points, exponential notation, or suffixes.
Examples: 500, -500, 5e2, 500.1, 5LL, 5ULL. Notes: Tarantool NUM data type is
unsigned, so -1 is understood as a large unsigned number.

Single-byte tokens are: , or ( or ) or arithmetic operators. Examples: * , ( ).

Tokens must be separated from each other by one or more spaces, except that
spaces are not necessary around single-byte tokens or string literals.

.. _setting delimiter:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                        Requests
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Generally requests are entered following the prompt in interactive mode while
``tarantool`` is running. (A prompt will be the word tarantool and a
greater-than sign, for example ``tarantool>``). The end-of-request marker is by
default a newline (line feed).

For multi-line requests, it is possible to change the end-of-request marker.
Syntax: :samp:`console = require('console'); console.delimiter({string-literal})`.
The string-literal must be a value in single quotes. Effect: string becomes
end-of-request delimiter, so newline alone is not treated as end of request.
To go back to normal mode: :samp:`console.delimiter(''){string-literal}`.
Delimiters are usually not necessary because Tarantool can tell when a
multi-line request has not ended (for example, if it sees that a function
declaration does not have an ``end`` keyword). Example:

.. code-block:: lua_tarantool

    console = require('console'); console.delimiter('!')
    function f ()
      statement_1 = 'a'
      statement_2 = 'b'
    end!
    console.delimiter('')!

For a condensed Backus-Naur Form [BNF] description of the suggested form
of client requests, see http://tarantool.org/doc/box-protocol.html.

In *interactive* mode, one types requests and gets results. Typically the
requests are typed in by the user following prompts. Here is an example of
an interactive-mode tarantool client session:

.. code-block:: tarantoolsession

    $ tarantool
    [ tarantool will display an introductory message
      including version number here ]
    tarantool> box.cfg{listen = 3301}
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
If Tarantool was installed with Debian or
Red Hat installation packages, the script is 
in :file:`/usr/bin/tarantoolctl` or :file:`/usr/local/bin/tarantoolctl`.
The script handles such things as:
starting, stopping, rotating logs, logging in to the application's console,
and checking status.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            configuring for tarantoolctl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The :program:`tarantoolctl` script will read a configuration file named
:file:`~/.config/tarantool/default`, or
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
        phia_dir = "/var/lib/tarantool",
        logger     = "/var/log/tarantool",
        username   = "tarantool",
    }
    instance_dir = "/etc/tarantool/instances.enabled"

The settings in the above script are:

``pid_file``
    The directory for the pid file and control-socket file. The
    script will add ":samp:`/{instance-name}`" to the directory name.

``wal_dir``
    The directory for the write-ahead :file:`*.xlog` files. The
    script will add ":samp:`/{instance-name}`" to the directory-name.

``snap_dir``
    The directory for the snapshot :file:`*.snap` files. The script
    will add ":samp:`/{instance-name}`" to the directory-name.

``phia_dir``
    The directory for the phia-storage-engine files. The script
    will add ":samp:`/phia/{instance-name}`" to the directory-name.

``logger``
    The place where the application log will go. The script will
    add ":samp:`/{instance-name}.log`" to the name.

``username``
    The user that runs the tarantool server. This is the operating-system
    user name rather than the Tarantool-client user name.

``instance_dir``
    The directory where all applications for this host are stored. The user
    who writes an application for :program:`tarantoolctl` must put the
    application's source code in this directory, or a symbolic link. For
    examples in this section the application name my_app will be used, and
    its source will have to be in :samp:`{instance_dir}/my_app.lua`.


~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
            commands for tarantoolctl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The command format is :samp:`tarantoolctl {operation} {application_name}`, where
operation is one of: start, stop, enter, logrotate, status, eval. Thus ...

.. option:: start <application>

    Start application *<application>*

.. option:: stop <application>

    Stop application

.. option:: enter <application>

    Show application's admin console

.. option:: logrotate <application>

    Rotate application's log files (make new, remove old)

.. option:: status <application>

    Check application's status

.. option:: eval <application> <scriptname>

    Execute code from *<scriptname>* on an instance of application

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
     typical code snippets for tarantoolctl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A user can check whether my_app is running with these lines:

.. code-block:: bash

    if tarantoolctl status my_app; then
    ...
    fi

A user can initiate, for boot time, an init.d set of instructions:

.. code-block:: bash

    for (each file mentioned in the instance_dir directory):
        tarantoolctl start `basename $ file .lua`

A user can set up a further configuration file for log rotation, like this:

.. cssclass:: highlight
.. parsed-literal::

    /path/to/tarantool/\*.log {
        daily
        size 512k
        missingok
        rotate 10
        compress
        delaycompress
        create 0640 tarantool adm
        postrotate
            /path/to/tarantoolctl logrotate `basename $ 1 .log`
        endscript
    }

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
      A detailed example for tarantoolctl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The example's objective is: make a temporary directory where tarantoolctl
can start a long-running application and monitor it.

The assumptions are: the root password is known, the computer is only being used
for tests, the Tarantool server is ready to run but is not currently running,
tarantoolctl is installed along the user's path,
and there currently is no directory named :file:`tarantool_test`.

Create a directory named /tarantool_test:

.. code-block:: console

    $ sudo mkdir /tarantool_test

Edit /etc/sysconfig/tarantool. It might be necessary to
say :codenormal:`sudo mkdir /etc/sysconfig` first. Let the new file contents be:

.. code-block:: lua

    default_cfg = {
        pid_file = "/tarantool_test/my_app.pid",
        wal_dir = "/tarantool_test",
        snap_dir = "/tarantool_test",
        phia_dir = "/tarantool_test",
        logger = "/tarantool_test/log",
        username = "tarantool",
    }
    instance_dir = "/tarantool_test"

Make the my_app application file, that is, :file:`/tarantool_test/my_app.lua`. Let the file contents be:

.. code-block:: lua

    box.cfg{listen = 3301}
    box.schema.user.passwd('Gx5!')
    box.schema.user.grant('guest','read,write,execute','universe')
    fiber = require('fiber')
    box.schema.space.create('tester')
    box.space.tester:create_index('primary',{})
    i = 0
    while 0 == 0 do
        fiber.sleep(5)
        i = i + 1
        print('insert ' .. i)
        box.space.tester:insert{i, 'my_app tuple'}
    end

Tell tarantoolctl to start the application ...

.. code-block:: console

    $ cd /tarantool_test
    $ sudo tarantoolctl start my_app

... expect to see messages indicating that the instance has started. Then ...

.. code-block:: console

    $ ls -l /tarantool_test/my_app

... expect to see the .snap file and the .xlog file. Then ...

.. code-block:: console

    $ sudo less /tarantool_test/log/my_app.log

... expect to see the contents of my_app's log, including error messages, if any. Then ...

.. code-block:: tarantoolsession

    $ cd /tarantool_test
    $ # assume that 'tarantool' invokes the tarantool server
    $ sudo tarantool
    tarantool> box.cfg{}
    tarantool> console = require('console')
    tarantool> console.connect('localhost:3301')
    tarantool> box.space.tester:select({0}, {iterator = 'GE'})

... expect to see several tuples that my_app has created.

Stop. The only clean way to stop my_app is with tarantoolctl, thus:

.. code-block:: console

    $ sudo tarantoolctl stop my_app

Clean up. Restore the original contents of :file:`/etc/sysconfig/tarantool`, and ...

.. code-block:: console

    $ cd /
    $ sudo rm -R tarantool_test

=====================================================================
            System-specific administration notes
=====================================================================

This section will contain information about issue or features which exist
on some platforms but not others - for example, on certain versions of a
particular Linux distribution.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    Administrating with Debian GNU/Linux and Ubuntu
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Setting up an instance:

.. code-block:: console

    $ ln -s /etc/tarantool/instances.available/instance-name.cfg /etc/tarantool/instances.enabled/

Starting all instances:

.. code-block:: console

    $ service tarantool start

Stopping all instances:

.. code-block:: console

    $ service tarantool stop

Starting/stopping one instance:

.. code-block:: console

    $ service tarantool-instance-name start/stop

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                 Fedora, RHEL, CentOS
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are no known permanent issues. For transient issues, go to
http://github.com/tarantool/tarantool/issues and enter "RHEL" or
"CentOS" or "Fedora" or "Red Hat" in the search box.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                       FreeBSD
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are no known permanent issues. For transient issues, go to
http://github.com/tarantool/tarantool/issues and enter "FreeBSD"
in the search box.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                       Mac OS X
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There are no known permanent issues. For transient issues, go to
http://github.com/tarantool/tarantool/issues and enter "OS X" in
the search box.

=====================================================================
                     Notes for systemd users
=====================================================================

The Tarantool package fully supports :program:`systemd` for managing instances and
supervising database daemons.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                     Instance management
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Tarantool package was designed to have multiple running instances of Tarantool
on the same machine. Use :samp:`systemctl {start|stop|restart|status} tarantool@${MYAPP}`
to manage your databases and Lua applications.

******************************************************
                 creating instances
******************************************************

Simply put your Lua configuration to :file:`/etc/tarantool/instances.available/${MYAPP}.lua`:

.. code-block:: lua

    box.cfg{listen = 3313}
    require('myappcode').start()

(this minimal example is sufficient).

Another starting point could be the :file:`example.lua` script that ships with Tarantool
and defines all options.

******************************************************
                starting instances
******************************************************

Use :samp:`systemctl start tarantool@${MYAPP}` to start ``${MYAPP}`` instance:

.. code-block:: console

    $ systemctl start tarantool@example
    $ ps axuf|grep exampl[e]
    taranto+  5350  1.3  0.3 1448872 7736 ?        Ssl  20:05   0:28 tarantool example.lua <running>

(console examples here and further on are for Fedora).

Use :samp:`systemctl enable tarantool@${MYAPP}` to enable ``${MYAPP}`` instance
for auto-load during system startup.

******************************************************
               monitoring instances
******************************************************

Use :samp:`systemctl status tarantool@${MYAPP}` to check information about
``${MYAPP}`` instance:

.. code-block:: console

    $ systemctl status tarantool@example
    tarantool@example.service - Tarantool Database Server
    Loaded: loaded (/etc/systemd/system/tarantool@.service; disabled; vendor preset: disabled)
    Active: active (running)
    Docs: man:tarantool(1)
    Process: 5346 ExecStart=/usr/bin/tarantoolctl start %I (code=exited, status=0/SUCCESS)
    Main PID: 5350 (tarantool)
    Tasks: 11 (limit: 512)
    CGroup: /system.slice/system-tarantool.slice/tarantool@example.service
    + 5350 tarantool example.lua <running>

Use :samp:`journalctl -u tarantool@${MYAPP}` to check the boot log:

.. code-block:: console

    $ journalctl -u tarantool@example -n 5
    -- Logs begin at Fri 2016-01-08 12:21:53 MSK, end at Thu 2016-01-21 21:17:47 MSK. --
    Jan 21 21:17:47 localhost.localdomain systemd[1]: Stopped Tarantool Database Server.
    Jan 21 21:17:47 localhost.localdomain systemd[1]: Starting Tarantool Database Server...
    Jan 21 21:17:47 localhost.localdomain tarantoolctl[5969]: /usr/bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    Jan 21 21:17:47 localhost.localdomain tarantoolctl[5969]: /usr/bin/tarantoolctl: Starting instance...
    Jan 21 21:17:47 localhost.localdomain systemd[1]: Started Tarantool Database Server

******************************************************
                attaching to instances
******************************************************

You can attach to a running Tarantool instance and evaluate some Lua code using the
:program:`tarantoolctl` utility:

.. code-block:: console

    $ tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> 1 + 1
    ---
    - 2
    ...
    unix/:/var/run/tarantool/example.control>

******************************************************
                    checking logs
******************************************************

Tarantool logs important events to :file:`/var/log/tarantool/${MYAPP}.log`.

Let's write something to the log file:

.. code-block:: console

    $ tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> require('log').info("Hello for README.systemd readers")
    ---
    ...

Then check the logs:

.. code-block:: console

    $ tail /var/log/tarantool/example.log
    2016-01-21 21:09:45.982 [5914] iproto I> binary: started
    2016-01-21 21:09:45.982 [5914] iproto I> binary: bound to 0.0.0.0:3301
    2016-01-21 21:09:45.983 [5914] main/101/tarantoolctl I> ready to accept requests
    2016-01-21 21:09:45.983 [5914] main/101/example I> Run console at /var/run/tarantool/example.control
    2016-01-21 21:09:45.984 [5914] main/101/example I> tcp_server: remove dead UNIX socket: /var/run/tarantool/example.control
    2016-01-21 21:09:45.984 [5914] main/104/console/unix/:/var/run/tarant I> started
    2016-01-21 21:09:45.985 [5914] main C> entering the event loop
    2016-01-21 21:14:43.320 [5914] main/105/console/unix/: I> client unix/: connected
    2016-01-21 21:15:07.115 [5914] main/105/console/unix/: I> Hello for README.systemd readers
    2016-01-21 21:15:09.250 [5914] main/105/console/unix/: I> client unix/: disconnected

Log rotation is enabled by default if you have :program:`logrotate` installed. Please configure
:file:`/etc/logrotate.d/tarantool` to change the default behavior.

******************************************************
                  stopping instances
******************************************************

Use :samp:`systemctl stop tarantool@${MYAPP}` to see information about the running
``${MYAPP}`` instance.

.. code-block:: console

    $ systemctl stop tarantool@example

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                Daemon supervision
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All instances are automatically restarted by :program:`systemd` in case of failure.

Let's try to destroy an instance:

.. code-block:: console

    $ systemctl status tarantool@example|grep PID
    Main PID: 5885 (tarantool)
    $ tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> os.exit(-1)
    /bin/tarantoolctl: unix/:/var/run/tarantool/example.control: Remote host closed connection

Now let's make sure that :program:`systemd` has revived our Tarantool instance:

.. code-block:: console

    $ systemctl status tarantool@example|grep PID
    Main PID: 5914 (tarantool)

Finally, let's check the boot logs:

.. code-block:: console

    $ journalctl -u tarantool@example -n 8
    -- Logs begin at Fri 2016-01-08 12:21:53 MSK, end at Thu 2016-01-21 21:09:45 MSK. --
    Jan 21 21:09:45 localhost.localdomain systemd[1]: tarantool@example.service: Unit entered failed state.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: tarantool@example.service: Failed with result 'exit-code'.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: tarantool@example.service: Service hold-off time over, scheduling restart.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: Stopped Tarantool Database Server.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: Starting Tarantool Database Server...
    Jan 21 21:09:45 localhost.localdomain tarantoolctl[5910]: /usr/bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    Jan 21 21:09:45 localhost.localdomain tarantoolctl[5910]: /usr/bin/tarantoolctl: Starting instance...
    Jan 21 21:09:45 localhost.localdomain systemd[1]: Started Tarantool Database Server.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
               Customizing the service file
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Please don't modify the :file:`tarantool@.service` file in-place, because it will be
overwritten during package upgrades. It is recommended to copy this file to
:file:`/etc/systemd/system` and then modify the required settings. Alternatively,
you can create a directory named :file:`unit.d/` within :file:`/etc/systemd/system` and
put there a drop-in file :file:`name.conf` that only changes the required settings.
Please see ``systemd.unit(5)`` manual page for additional information.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                      Debugging
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:program:`coredumpctl` automatically saves core dumps and stack traces in case of a crash.
Here is how it works:

.. code-block:: console

    $ # !!! please never do this on the production system !!!
    $ tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> require('ffi').cast('char *', 0)[0] = 48
    /bin/tarantoolctl: unix/:/var/run/tarantool/example.control: Remote host closed connection

:samp:`coredumpctl list /usr/bin/tarantool` displays the latest crashes of the Tarantool daemon:

.. code-block:: console

    $ coredumpctl list /usr/bin/tarantool
    MTIME                            PID   UID   GID SIG PRESENT EXE
    Sat 2016-01-23 15:21:24 MSK   20681  1000  1000   6   /usr/bin/tarantool
    Sat 2016-01-23 15:51:56 MSK   21035   995   992   6   /usr/bin/tarantool

:samp:`coredumpctl info <pid>` shows the stack trace and other useful information:

.. code-block:: console

    $ coredumpctl info 21035
              PID: 21035 (tarantool)
              UID: 995 (tarantool)
              GID: 992 (tarantool)
           Signal: 6 (ABRT)
        Timestamp: Sat 2016-01-23 15:51:42 MSK (4h 36min ago)
     Command Line: tarantool example.lua <running>
       Executable: /usr/bin/tarantool
    Control Group: /system.slice/system-tarantool.slice/tarantool@example.service
             Unit: tarantool@example.service
            Slice: system-tarantool.slice
          Boot ID: 7c686e2ef4dc4e3ea59122757e3067e2
       Machine ID: a4a878729c654c7093dc6693f6a8e5ee
         Hostname: localhost.localdomain
          Message: Process 21035 (tarantool) of user 995 dumped core.

                   Stack trace of thread 21035:
                   #0  0x00007f84993aa618 raise (libc.so.6)
                   #1  0x00007f84993ac21a abort (libc.so.6)
                   #2  0x0000560d0a9e9233 _ZL12sig_fatal_cbi (tarantool)
                   #3  0x00007f849a211220 __restore_rt (libpthread.so.0)
                   #4  0x0000560d0aaa5d9d lj_cconv_ct_ct (tarantool)
                   #5  0x0000560d0aaa687f lj_cconv_ct_tv (tarantool)
                   #6  0x0000560d0aaabe33 lj_cf_ffi_meta___newindex (tarantool)
                   #7  0x0000560d0aaae2f7 lj_BC_FUNCC (tarantool)
                   #8  0x0000560d0aa9aabd lua_pcall (tarantool)
                   #9  0x0000560d0aa71400 lbox_call (tarantool)
                   #10 0x0000560d0aa6ce36 lua_fiber_run_f (tarantool)
                   #11 0x0000560d0a9e8d0c _ZL16fiber_cxx_invokePFiP13__va_list_tagES0_ (tarantool)
                   #12 0x0000560d0aa7b255 fiber_loop (tarantool)
                   #13 0x0000560d0ab38ed1 coro_init (tarantool)
                   ...

:samp:`coredumpctl -o filename.core info <pid>` saves the core dump into a file.

:samp:`coredumpctl gdb <pid>` starts :program:`gdb` on the core dump.

It is highly recommended to install the ``tarantool-debuginfo`` package to improve
:program:`gdb` experience. Example:

.. code-block:: console

    $ dnf debuginfo-install tarantool

.. $ # for CentOS
.. $ yum install tarantool-debuginfo

:program:`gdb` also provides information about the ``debuginfo`` packages you need to install:

.. code-block:: console

    $ # gdb -p <pid>
    ...
    Missing separate debuginfos, use: dnf debuginfo-install
    glibc-2.22.90-26.fc24.x86_64 krb5-libs-1.14-12.fc24.x86_64
    libgcc-5.3.1-3.fc24.x86_64 libgomp-5.3.1-3.fc24.x86_64
    libselinux-2.4-6.fc24.x86_64 libstdc++-5.3.1-3.fc24.x86_64
    libyaml-0.1.6-7.fc23.x86_64 ncurses-libs-6.0-1.20150810.fc24.x86_64
    openssl-libs-1.0.2e-3.fc24.x86_64

Symbol names are present in stack traces even if you don't have the ``tarantool-debuginfo`` package installed.

For additional information, please refer to the documentation provided with your Linux distribution.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
                     Precautions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

* Please don't use ``tarantoolctl {start,stop,restart}`` to control instances
  started by :program:`systemd`. It is still possible to use :program:`tarantoolctl` to start and
  stop instances from your local directories (e.g. :file:`${HOME}`) without obtaining ``ROOT`` access.

* :program:`tarantoolctl` is configured to work properly with ;program:`systemd`. Please don't
  modify system-wide settings of :program:`tarantoolctl`, such as paths, directory permissions and usernames.
  Otherwise, you have a chance to shoot yourself in the foot.

* :program:`systemd` scripts are maintained by the Tarantool Team (http://tarantool.org).
  Please file tickets directly to the upstream's bug tracker rather than to your Linux distribution.

=====================================================================
             Updating Tarantool in production
=====================================================================

First, put your application's business logic in a Tarantool-Lua module that exports its functions for CALL.

For example, :file:`/usr/share/tarantool/myapp.lua`:

.. code-block:: lua

    local function start()
    -- Initial version
    box.once("myapp:.1.0", function()
    box.schema.space.create("somedata")
    box.space.somedata:create_index("primary")
    ...
    end

    -- migration code from 1.0 to 1.1
    box.once("myapp:.v1.1", function()
    box.space.somedata.index.primary:alter(...)
    ...
    end

    -- migration code from 1.1 to 1.2
    box.once("myapp:.v1.2", function()
    box.space.somedata.space:alter(...)
    box.space.somedata:insert(...)
    ...
    end

    -- start some background fibers if you need

    local function stop()
    -- stop all background fibers and cleanup resources
    end

    local function api_for_call(xxx)
    -- do some business
    end

    return {
    start = start;
    stop = stop;
    api_for_call = api_for_call;
    }

This file is maintained by the application's developers. On its side,
Tarantool Team provides templates for you to `assemble deb/rpm packages`_
and utilities to quickly `assemble packages for specific platforms`_.
If needed, you can split applications into standalone files and/or modules.

.. _assemble deb/rpm packages: https://github.com/tarantool/modulekit
.. _assemble packages for specific platforms: https://github.com/tarantool/build


Second, put an initialization script to the :file:`/etc/tarantool/instances.available` directory.

For example, :file:`/etc/tarantool/instances.available/myappcfg.lua`:

.. code-block:: lua

    #!/usr/bin/env tarantool

    box.cfg {
    listen = 3301;
    }

    if myapp ~= nil then
    -- hot code reload using tarantoolctl or dofile()

    -- unload old application
    myapp.stop()
    -- clear cache for loaded modules and dependencies
    package.loaded['myapp'] = nil
    package.loaded['somedep'] = nil; -- dependency of 'myapp'
    end

    -- load a new version of app and all dependencies
    myapp = require('myapp').start({some app options controlled by sysadmins})


As a more detailed example, you can take the :file:`example.lua` script that ships with Tarantool
and defines all configuration options.

This initialization script is actually a configuration file and should be maintained by system
administrators, while developers only provide a template.


Now update your app file in :file:`/usr/share/tarantool`. Replace your application file
(for example, :file:`/usr/share/tarantool/myapp.lua`) and manually reload
the :file:`myappcfg.lua` initialization script using :program:`tarantoolctl`:

.. code-block:: console

    $ tarantoolctl eval /etc/tarantool/instance.enabled/myappcfg.lua

After that, you need to manually flush the cache of ``package.loaded`` modules.

For deb/rpm packages, you can add the ``tarantoolctl eval`` instruction directly into Tarantool's
specification in :file:`RPM.spec` and the :file:`/debian` directory.

Finally, clients make a CALL to ``myapp.api_for_call`` and other API functions.

In the case of ``tarantool-http``, there is no need to start the binary protocol at all.

.. _modules-luarocks-and-requiring-packages:

=====================================================================
       Modules, LuaRocks, and requiring packages
=====================================================================

To extend Tarantool there are packages, which are also called "modules",
which in Lua are also called "rocks".
Users who are unfamiliar with Lua modules may benefit from following
the Lua-Modules-Tutorial_
before reading this section.

**Install a module**

The modules that come from Tarantool developers and community contributors are
on rocks.tarantool.org_. Some of them
-- :ref:`expirationd <package-expirationd>`,
:ref:`mysql <d-plugins-mysql-example>`,
:ref:`postgresql <d-plugins-postgresql-example>`,
:ref:`shard <package-shard>` --
are discussed elsewhere in this manual.

Step 1: Install LuaRocks.
A general description for installing LuaRocks on a Unix system is in
the LuaRocks-Quick-Start-Guide_.
For example on Ubuntu one could say: |br|
:codenormal:`$` :codebold:`sudo apt-get install luarocks`

Step 2: Add the Tarantool repository to the list of rocks servers.
This is done by putting rocks.tarantool.org in the .luarocks/config.lua file: |br|
:codenormal:`$` :codebold:`mkdir ~/.luarocks` |br|
:codenormal:`$` :codebold:`echo "rocks_servers = {[[http://rocks.tarantool.org/]]}" >> ~/.luarocks/config.lua` |br|

Once these steps are complete, the repositories can be searched with |br|
:codenormal:`$` :codebold:`luarocks search` :codeitalic:`module-name` |br|
and new modules can be added to the local repository with |br|
:codenormal:`$` :codebold:`luarocks install` :codeitalic:`module-name` :codenormal:`--local` |br|
and any package/module can be loaded for Tarantool with |br|
:codenormal:`tarantool>` :codeitalic:`local-name` :codenormal:`=` :codebold:`require('`:codeitalic:`module-name`:codenormal:`')` |br|
... and that is why the examples in the manual's Packages section often begin with `require` requests.
See rocks_ on github.com/tarantool for more examples
and information about contributing.

**Example: making a new Lua module locally**

In this example, create a new Lua file named `mymodule.lua`,
containing a named function which will be exported.
Then, in Tarantool: load, examine, and call.

The Lua file should look like this:

.. code-block:: lua

    -- mymodule - a simple Tarantool module
    local exports = {}
    exports.myfun = function(input_string)
        print('Hello', input_string)
    end
    return exports

The requests to load and examine and call look like this: |br|
:codenormal:`tarantool>`:codebold:`mymodule = require('mymodule')` |br|
:codenormal:`>---` |br|
:codenormal:`>...` |br|
|br|
:codenormal:`tarantool>`:codebold:`mymodule` |br|
:codenormal:`---` |br|
:codenormal:`>- myfun: 'function: 0x405edf20'` |br|
:codenormal:`>...` |br|
:codenormal:`tarantool>`:codebold:`mymodule.myfun(os.getenv('USER'))` |br|
:codenormal:`Hello world` |br|
:codenormal:`>---` |br|
:codenormal:`>...`

**Example: making a new C/C++ module locally**

In this example, create a new C file named `mycmodule.c`,
containing a named function which will be exported.
Then, in Tarantool: load, examine, and call.

Prerequisite: install `tarantool-dev` first.

The C file should look like this:

.. code-block:: bash

    /* mycmodule - a simple Tarantool module */
    #include <lua.h>
    #include <lauxlib.h>
    #include <lualib.h>
    #include <tarantool.h>
    static int
    myfun(lua_State *L)
    {
        if (lua_gettop(L) < 1)
            return luaL_error(L, "Usage: myfun(name)");

        /* Get first argument */
        const char *name = lua_tostring(L, 1);

        /* Push one result to Lua stack */
        lua_pushfstring(L, "Hello, %s", name);
        return 1; /* the function returns one result */
    }
    LUA_API int
    luaopen_mycmodule(lua_State *L)
    {
        static const struct luaL_reg reg[] = {
            { "myfun", myfun },
            { NULL, NULL }
        };
        luaL_register(L, "mycmodule", reg);
        return 1;
    }

Use :codenormal:`gcc` to compile the code for a shared library (without a "lib" prefix),
then use :codenormal:`ls` to examine it: |br|

:codenormal:`$` :codebold:`gcc mycmodule.c -shared -fPIC -I/usr/include/tarantool -o mycmodule.so` |br|
:codenormal:`$` :codebold:`ls mycmodule.so -l` |br|
:codenormal:`-rwxr-xr-x 1 roman roman 7272 Jun  3 16:51 mycmodule.so`

Tarantool's developers recommend use of Tarantool's CMake-scripts_
which will handle some of the build steps automatically.

The requests to load and examine and call look like this: |br|
:codenormal:`tarantool>`:codebold:`myсmodule = require('myсmodule')` |br|
:codenormal:`---` |br|
:codenormal:`...` |br|
:codenormal:`tarantool>`:codebold:`myсmodule` |br|
:codenormal:`---` |br|
:codenormal:`- myfun: 'function: 0x4100ec98'` |br|
:codenormal:`...` |br|
:codenormal:`tarantool>`:codebold:`mycmodule.myfun(os.getenv('USER'))` |br|
:codenormal:`---` |br|
:codenormal:`- Hello, world` |br|
:codenormal:`...` |br|

One can also make modules with C++, provided that the code does not throw exceptions.

**Tips for special situations**

Lua caches all loaded modules in the :code:`package.loaded` table.
To reload a module from disk, set its key to `nil`: |br|
:codenormal:`tarantool>` :codebold:`package.loaded['`:codeitalic:`modulename`:codebold:`'] = nil`

Use ``package.path`` to search for ``.lua`` modules, and use
``package.cpath`` to search for C binary modules. |br|
:codenormal:`tarantool>`:codebold:`package.path` |br|
:codenormal:`---` |br|
:codenormal:`- ./?.lua;./?/init.lua;/home/roman/.luarocks/share/lua/5.1/?.lua;/home/roman/.luarocks/share/lua/5.1/?/init.lua;/home/roman/.luarocks/share/lua/?.lua;/home/roman/.luarocks/share/lua/?/init.lua;/usr/share/tarantool/?.lua;/usr/share/tarantool/?/init.lua;./?.lua;/usr/local/share/luajit-2.0.3/?.lua;/usr/local/share/lua/5.1/?.lua;/usr/local/share/lua/5.1/?/init.lua` |br|
:codenormal:`...`
:codenormal:`tarantool>`:codebold:`package.cpath` |br|
:codenormal:`---` |br|
:codenormal:`- ./?.so;/home/roman/.luarocks/lib/lua/5.1/?.so;/home/roman/.luarocks/lib/lua/?.so;/usr/lib/tarantool/?.so;./?.so;/usr/local/lib/lua/5.1/?.so;/usr/local/lib/lua/5.1/loadall.so` |br|
:codenormal:`...` |br|
Substitute question-mark with :code:`modulename` when calling
:code:`require('modulename')`.

To see the internal state from within a Lua module, use `state`
and create a local variable inside the scope of the file:

.. code-block:: lua

    -- mymodule
    local exports = {}
    local state = {}
    exports.myfun = function()
        state.x = 42 -- use state
    end
    return exports

Notice that the Lua examples use local variables.
Use global variables with caution, since the module's users
may be unaware of them.

To see a sample Lua + C module, go to http_ on github.com/tarantool.

.. _Lua-Modules-Tutorial: http://lua-users.org/wiki/ModulesTutorial
.. _LuaRocks: http://rocks.tarantool.org
.. _LuaRocks-Quick-Start-Guide: http://luarocks.org/#quick-start
.. _rocks.tarantool.org: http://rocks.tarantool.org
.. _rocks: github.com/tarantool/rocks <https://github.com/tarantool/rocks
.. _CMake-scripts: https://github.com/tarantool/http
.. _http: https://github.com/tarantool/http
