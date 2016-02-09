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

## Debugging

`coredumpctl(1)` automatically saves coredumps and stack traces in case of
crash. Let's see how does it work:

    # tarantoolctl enter example
    /bin/tarantoolctl: Found example.lua in /etc/tarantool/instances.available
    /bin/tarantoolctl: Connecting to /var/run/tarantool/example.control
    /bin/tarantoolctl: connected to unix/:/var/run/tarantool/example.control
    -- !!! please never do this on the production system
    unix/:/var/run/tarantool/example.control> require('ffi').cast('char *', 0)[0] = 48
    /bin/tarantoolctl: unix/:/var/run/tarantool/example.control: Remote host closed connection

`coredump list /usr/bin/tarantool` displays last crashes of Tarantool daemon:

    # coredumpctl list /usr/bin/tarantool
    MTIME                            PID   UID   GID SIG PRESENT EXE
    Sat 2016-01-23 15:21:24 MSK   20681  1000  1000   6   /usr/bin/tarantool
    Sat 2016-01-23 15:51:56 MSK   21035   995   992   6   /usr/bin/tarantool

`coredump info <pid>` show a stack trace and other useful information:

```
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
```

`coredumpctl -o filename.core info <pid>` saves the coredump into a file.

`coredumpctl gdb <pid>` starts  `gdb` on the coredump.

It is highly recommended to install tarantool-debuginfo package to improve
gdb experience (example below for Fedora):

    dnf debuginfo-install tarantool

gdb provides information about `debuginfo` paackages you need to install:

    # gdb -p <pid>
    ...
    Missing separate debuginfos, use: dnf debuginfo-install
    glibc-2.22.90-26.fc24.x86_64 krb5-libs-1.14-12.fc24.x86_64
    libgcc-5.3.1-3.fc24.x86_64 libgomp-5.3.1-3.fc24.x86_64
    libselinux-2.4-6.fc24.x86_64 libstdc++-5.3.1-3.fc24.x86_64
    libyaml-0.1.6-7.fc23.x86_64 ncurses-libs-6.0-1.20150810.fc24.x86_64
    openssl-libs-1.0.2e-3.fc24.x86_64

Symbol names are present in stack traces even if you don't have
`tarantool-debuginfo` package installed.

Please refer to the documentation provided by your distribution for additional
information.

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
