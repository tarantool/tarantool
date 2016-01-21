# Notes for Systemd Users

Tarantool package fully supports **systemd** for managing instances and
supervising database daemons.

## Instance Management

Package was designed to have multiple running instances of Tarantool on
the same machine. Please use
`systemctl {start|stop|restart|status} tarantool@${MYAPP}` to manage your
databases and Lua applications.

### Creating Instances

Simple put your Lua configuration to
`/etc/tarantool/instances.available/${MYAPP}.lua`:

    box.cfg {
        slab_alloc_arena = 1.0; -- 1Gb
        listen = 3313;
    }

    require('myappcode').start()

Tarantool ships with `example.lua` script which can be used as a start point.

### Starting Instances

Use `systemctl start tarantool@${MYAPP}` to start `${MYAPP}` instance:

    # systemctl start tarantool@example
    # ps axuf|grep exampl[e]
    taranto+  5350  1.3  0.3 1448872 7736 ?        Ssl  20:05   0:28 tarantool example.lua <running>

Use `systemctl enable tarantool@${MYAPP}` to enable `${MYAPP}` instance
for auto-load during system startup.

### Monitoring Instances

Use `systemctl status tarantool@${MYAPP}` to check information about
`${MYAPP}` instance:

    # systemctl status tarantool@example
    ● tarantool@example.service - Tarantool Database Server
       Loaded: loaded (/etc/systemd/system/tarantool@.service; disabled; vendor preset: disabled)
       Active: active (running)
         Docs: man:tarantool(1)
      Process: 5346 ExecStart=/usr/bin/tarantoolctl start %I (code=exited, status=0/SUCCESS)
     Main PID: 5350 (tarantool)
        Tasks: 11 (limit: 512)
       CGroup: /system.slice/system-tarantool.slice/tarantool@example.service
               + 5350 tarantool example.lua <running>

Use `journalctl -u tarantool@${MYAPP}` to check boot log:

    journalctl -u tarantool@example -n 5
    -- Logs begin at Fri 2016-01-08 12:21:53 MSK, end at Thu 2016-01-21 21:17:47 MSK. --
    Jan 21 21:17:47 localhost.localdomain systemd[1]: Stopped Tarantool Database Server.
    Jan 21 21:17:47 localhost.localdomain systemd[1]: Starting Tarantool Database Server...
    Jan 21 21:17:47 localhost.localdomain tarantoolctl[5969]: /usr/bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    Jan 21 21:17:47 localhost.localdomain tarantoolctl[5969]: /usr/bin/tarantoolctl: Starting instance...
    Jan 21 21:17:47 localhost.localdomain systemd[1]: Started Tarantool Database Server


### Attaching to Instances

It is possible to attach to a running Tarantool instance and evaluate some
Lua code using `tarantoolctl` utility:

    # tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> 1 + 1
    ---
    - 2
    ...
    unix/:/var/run/tarantool/example.control>


### Checking Logs

Tarantool log important events to `/var/log/tarantool/${MYAPP}.log`.

Let's write something to the log file:

    # tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> require('log').info("Hello for README.systemd readers")
    ---
    ...

Then check the logs:

    # tail /var/log/tarantool/example.log
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

Log rotation is enabled by default if you have `logrotate` installed.

Please tweak `/etc/logrotate.d/tarantool` to change the default behavior.

### Stopping Instance

Use `systemctl stop tarantool@${MYAPP}` to see information about running
`${MYAPP}` instance.

    # systemctl stop tarantool@example

## Daemon Supervision

All instances are automatically restarted by `systemd` in case of failure.

Let's try to destroy an instance:

    # systemctl status tarantool@example|grep PID
     Main PID: 5885 (tarantool)
    # tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    unix/:/var/run/tarantool/example.control> os.exit(-1)
    /bin/tarantoolctl: unix/:/var/run/tarantool/example.control: Remote host closed connection

`systemd` has revived our Tarantool:

    # systemctl status tarantool@example|grep PID
     Main PID: 5914 (tarantool)

Let's check the boot logs:

    # journalctl -u tarantool@example -n 8
    -- Logs begin at Fri 2016-01-08 12:21:53 MSK, end at Thu 2016-01-21 21:09:45 MSK. --
    Jan 21 21:09:45 localhost.localdomain systemd[1]: tarantool@example.service: Unit entered failed state.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: tarantool@example.service: Failed with result 'exit-code'.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: tarantool@example.service: Service hold-off time over, scheduling restart.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: Stopped Tarantool Database Server.
    Jan 21 21:09:45 localhost.localdomain systemd[1]: Starting Tarantool Database Server...
    Jan 21 21:09:45 localhost.localdomain tarantoolctl[5910]: /usr/bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    Jan 21 21:09:45 localhost.localdomain tarantoolctl[5910]: /usr/bin/tarantoolctl: Starting instance...
    Jan 21 21:09:45 localhost.localdomain systemd[1]: Started Tarantool Database Server.

## Customizing Service File

Please don't modify `tarantool@.service` file in-place, because it will be
overwrriten during package upgrades. It is recommended to copy this file to
`/etc/systemd/system` and then modify the chosen settings. Alternatively,
one can create a directory named `unit.d/` within `/etc/systemd/system` and
place a drop-in file name.conf there that only changes the specific
settings one is interested in. Please see systemd.unit(5) manual page for
additional information.

## Precautions

* Please don't use `tarantoolctl {start,stop,restart}` to control instances
  started by systemd. It is still possible to use `tarantoolctl` to start and
  stop instances from your local directories (e.g. `${HOME}`) without
  obtaining `ROOT` access.

* `tarantoolctl` is configured to work properly with **systemd**. Please don't
   modify system-wide settings of `tarantoolctl`, such as paths, directory
   permissions and usernames. Otherwise, you have a chance to shoot yourself
   in the foot.

* systemd scripts are maintained by Tarantool Team (http://tarantool.org).
  Please file tickets directly to the upstream's bug tracker rather than to
  your Linux distribution.
