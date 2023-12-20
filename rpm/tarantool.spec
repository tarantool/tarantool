# XXX: There is an old CMake (2.8.12) provided by cmake package in
# main CentOS 7 repositories. At the same time, there is a newer
# package cmake3 providing CMake 3+ from EPEL repository. So, one
# need to use cmake3 package to build Tarantool on old systems.
%define use_cmake3 0%{?rhel} == 7

# Get ${GC64} env variable which can keep the value of 'true' or 'false' to
# enable or disable luajit gc64.
%define _gc64 "%{getenv:GC64}"

%if %use_cmake3
# XXX: Unfortunately there is no way to make rpmbuild install and
# enable EPEL repository prior to the build step. However, the
# requirement below obligues user to enable EPEL by himself,
# otherwise this dependency is left unmet. If there are any issues
# with building an RPM package on RHEL/CentOS 7 read the docs:
# https://www.tarantool.io/en/doc/latest/dev_guide/building_from_source/
BuildRequires: cmake3 >= 3.3
%else
BuildRequires: cmake >= 3.3
%endif

BuildRequires: make
%if (0%{?fedora} >= 22 || 0%{?rhel} >= 7)
BuildRequires: gcc >= 4.5
BuildRequires: gcc-c++ >= 4.5
%endif
BuildRequires: coreutils
BuildRequires: sed
BuildRequires: readline-devel
BuildRequires: openssl-devel
BuildRequires: libicu-devel
#BuildRequires: msgpuck-devel
%if 0%{?fedora} > 0
# pod2man is needed to build man pages
BuildRequires: perl-podlators
%endif
Requires(pre): %{_sbindir}/useradd
Requires(pre): %{_sbindir}/groupadd

# libcurl dependencies (except ones we have already).
BuildRequires: zlib-devel
Requires: zlib

#
# Disable stripping of /usr/bin/tarantool to allow the debug symbols
# in runtime. Tarantool uses the debug symbols to display fiber's stack
# traces in fiber.info().
#
%global _enable_debug_package 0
%global debug_package %{nil}
%global __os_install_post /usr/lib/rpm/brp-compress %{nil}
%global __strip /bin/true

# Set dependences for tests.
BuildRequires: python3
BuildRequires: python3-six
BuildRequires: python3-gevent
BuildRequires: python3-pyyaml
# needed for datetime tests
BuildRequires: tzdata

# Install prove to run LuaJIT tests.
BuildRequires: perl-Test-Harness

# For third-party subprojects that are built with autotools.
BuildRequires: autoconf
BuildRequires: automake
BuildRequires: libtool

Name: tarantool
# ${major}.${major}.${minor}.${patch}, e.g. 1.6.8.175
# Version is updated automaically using git describe --long --always
Version: 1.7.2.385
Release: 1%{?dist}
Group: Applications/Databases
Summary: In-memory database and Lua application server
License: BSD

Provides: tarantool-debuginfo = %{version}-%{release}
Provides: tarantool-common = %{version}-%{release}
Obsoletes: tarantool-common < 1.6.8.434-1
# Add dependency on network configuration files used by `socket` module
# https://github.com/tarantool/tarantool/issues/1794
Requires: /etc/protocols
Requires: /etc/services
# Deps for built-in package manager
# https://github.com/tarantool/tarantool/issues/2612
Requires: openssl
Requires: tzdata
%if (0%{?fedora} >= 22 || 0%{?rhel} >= 8)
# RHEL <= 7 doesn't support Recommends:
Recommends: tarantool-devel
Recommends: git-core
Recommends: cmake >= 3.3
Recommends: make
Recommends: gcc >= 4.5
Recommends: gcc-c++ >= 4.5
Recommends: tt
%endif

URL: http://tarantool.org
Source0: http://download.tarantool.org/tarantool/1.7/src/tarantool-%{version}.tar.gz
%description
Tarantool is a high performance in-memory NoSQL database and Lua
application server. Tarantool supports replication, online backup and
stored procedures in Lua.

This package provides the server daemon and admin tools.

%package devel
Summary: Server development files for %{name}
Group: Applications/Databases
Requires: %{name}%{?_isa} = %{version}-%{release}
%description devel
Tarantool is a high performance in-memory NoSQL database and Lua
application server. Tarantool supports replication, online backup and
stored procedures in Lua.

This package provides server development files needed to create
C and Lua/C modules.

%prep
%setup -q -n %{name}-%{version}

%build
# RHBZ #1301720: SYSCONFDIR an LOCALSTATEDIR must be specified explicitly
# Must specified explicitly since Fedora 33:
# 1. -B .
#    because for now binary path by default value like:
#      '-B x86_64-redhat-linux-gnu'
# 2. -DENABLE_LTO=ON
#    because for now LTO flags are set in CC/LD flags by default:
#      '-flto=auto -ffat-lto-objects'
# XXX: KISS, please. I can play with RPM macros to redefine cmake
# macro for cmake3 usage, but it totally doesn't worth it.
%if %use_cmake3
%cmake3 \
%else
%cmake \
%endif
       -B . \
         -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=%{_localstatedir} \
         -DCMAKE_INSTALL_SYSCONFDIR:PATH=%{_sysconfdir} \
%if 0%{?fedora} >= 33
         -DENABLE_LTO=ON \
%endif
%if %{_gc64} == "true"
         -DLUAJIT_ENABLE_GC64:BOOL=ON \
%endif
         -DENABLE_WERROR:BOOL=ON \
         -DENABLE_DIST:BOOL=ON
make %{?_smp_mflags}

%install
%make_install
# %%doc and %%license macroses are used instead
rm -rf %{buildroot}%{_datarootdir}/doc/tarantool/

%if "%{getenv:MAKE_CHECK}" != "false"
%check
make test-force
%endif

%pre
/usr/sbin/groupadd -r tarantool > /dev/null 2>&1 || :
/usr/sbin/useradd -M %{?rhel:-N} -g tarantool -r -d /var/lib/tarantool -s /sbin/nologin\
    -c "Tarantool Server" tarantool > /dev/null 2>&1 || :

%files
%{_bindir}/tarantool
%{_mandir}/man1/tarantool.1*
%doc README.md
%{!?_licensedir:%global license %doc}
%license LICENSE AUTHORS

%files devel
%dir %{_includedir}/tarantool
%{_includedir}/tarantool/lauxlib.h
%{_includedir}/tarantool/lmisclib.h
%{_includedir}/tarantool/luaconf.h
%{_includedir}/tarantool/lua.h
%{_includedir}/tarantool/lua.hpp
%{_includedir}/tarantool/luajit.h
%{_includedir}/tarantool/lualib.h
%{_includedir}/tarantool/module.h
%{_includedir}/tarantool/curl

%changelog
* Thu Aug 19 2021 Kirill Yukhin <kyukhin@tarantool.org> 2.8.2.0-1
- Introduced support for LJ_DUALNUM mode in luajit-gdb.py.
- The new method table.equals compares two tables by value with respect
  to the __eq metamethod.
- The log module now supports symbolic representation of log levels.
- Descriptions of type mismatch error and inconsistent type error
  have become more informative.
- Removed explicit cast from BOOLEAN to numeric types and vice versa.
- Removed explicit cast from VARBINARY to numeric types and vice versa.
- Fixed a bug due to which a string that is not NULL terminated
  could not be cast to BOOLEAN, even if the conversion should
  be successful according to the rules.
- fiber.wakeup() in Lua and fiber_wakeup() in C became NOP on
  the currently running fiber.
- Various bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.8.2

* Thu Aug 19 2021 Kirill Yukhin <kyukhin@tarantool.org> 2.7.3.0-1
- Provide information about state of synchronous replication via
  box.info.synchro interface.
- Introduced support for LJ_DUALNUM mode in luajit-gdb.py.
- The new method table.equals compares two tables by value with respect
  to the __eq metamethod.
- Descriptions of type mismatch error and inconsistent type error
  have become more informative.
- Removed explicit cast from BOOLEAN to numeric types and vice versa.
- Removed explicit cast from VARBINARY to numeric types and vice versa.
- Fixed a bug due to which a string that is not NULL terminated
  could not be cast to BOOLEAN, even if the conversion should
  be successful according to the rules.
- fiber.wakeup() in Lua and fiber_wakeup() in C became NOP on
  the currently running fiber.
- Various bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.7.3

* Thu Aug 19 2021 Kirill Yukhin <kyukhin@tarantool.org> 1.10.11.0-1
- Introduced support for LJ_DUALNUM mode in luajit-gdb.py.
- fiber.wakeup() in Lua and fiber_wakeup() in C became
- NOP on the currently running fiber.
- Fixed memory leak on each box.on_commit() and box.on_rollback().
- Fixed invalid results produced by json module's encode function when it
  was used from the Lua garbage collector.
- Fixed a bug when iterators became invalid after schema change.
- Fixed crash in case of reloading a compiled module when the
  new module lacks some of functions which were present in the
  former code.
- Fixed console client connection breakage if request times out.
- Added missing broadcast to net.box.future:discard() so that now
  fibers waiting for a request result are woken up when the request
  is discarded.
- Fix possible keys divergence during secondary index build which might
  lead to missing tuples in it.
- Fix crash which may occur while switching read_only mode due to
  duplicating transaction in tx writer list.
- Fixed a race between Vinyl garbage collection and compaction
  resulting in broken vylog and recovery.
- Fix replication stopping occasionally with ER_INVALID_MSGPACK when
  replica is under high load.
- Fixed optimization for single-char strings in IR_BUFPUT
  assembly routine.
- Fixed slots alignment in lj-stack command output when
  LJ_GC64 is enabled.
- Fixed dummy frame unwinding in lj-stack command.
- Fixed detection of inconsistent renames even in the presence
  of sunk values.
- Fixed the order VM registers are allocated by LuaJIT frontend
  in case of BC_ISGE and BC_ISGT.
- When error is raised during encoding call results,
  auxiliary lightuserdata value is not removed from the main
  Lua coroutine stack.
- Fixed Lua C API misuse, when the error is raised during call results
  encoding on unprotected coroutine and expected to be catched
  on the different one, that is protected.
- Fixed possibility crash in case when trigger removes itself.
- Fixed possibility crash in case when someone destroy trigger,
  when it's yield.

* Wed Apr 21 2021 Kirill Yukhin <kyukhin@tarantool.org> 2.8.1.0-1
- Implement ability to run multiple iproto threads.
- Set box.cfg options with environment variables.
- Introduce box.ctl.promote() and the concept of manual elections.
- Lua memory profiler enhancements.
- Many other features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.8.1

* Wed Apr 21 2021 Kirill Yukhin <kyukhin@tarantool.org> 2.7.2.0-1
- Introduce the concept of WAL queue and a new configuration option:
  wal_queue_max_size.
- Introduce box.ctl.promote() and the concept of manual elections.
- Updated CMake minimum required version to 3.1.
- Drop autotools dependencies.
- Bump built-in zstd version from v1.3.3 to v1.4.8.
- Enable smtp and smtps protocols in bundled libcurl.
- Ship libcurl headers to system path "${PREFIX}/include/tarantool"
  in the case of libcurl included as bundled library or static build.
- Various bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.7.2

* Wed Apr 21 2021 Kirill Yukhin <kyukhin@tarantool.org> 2.6.3.0-1
- Introduce the concept of WAL queue and a new configuration option:
  wal_queue_max_size.
- Introduce box.ctl.promote() and the concept of manual elections.
- Updated CMake minimum required version to 3.1.
- Drop autotools dependencies.
- Stop publishing new binary packages for Debian Jessie.
- Bump built-in zstd version from v1.3.3 to v1.4.8.
- Enable smtp and smtps protocols in bundled libcurl.
- Ship libcurl headers to system path "${PREFIX}/include/tarantool"
  in the case of libcurl included as bundled library or static build.
- Various bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.6.3

* Wed Apr 21 2021 Kirill Yukhin <kyukhin@tarantool.org> 1.10.10.0-1
- Updated CMake minimum required version in Tarantool
  build infrastructure to 3.1.
- Stop publishing new binary packages for Debian Jessie.
- Backported -DENABLE_LTO=ON/OFF cmake option.
- Bump built-in zstd version from v1.3.3 to v1.4.8.
- libcurl symbols in the case of bundled libcurl are now exported.
- Enable smtp and smtps protocols in bundled libcurl.
- Ship libcurl headers to system path "${PREFIX}/include/tarantool"
  in the case of libcurl included as bundled library or static build.
- Extensive usage of uri and uuid modules with debug log level could lead to
  a crash or corrupted result of the functions from these modules.
  The same could happen with some functions from the modules fio,
  box.tuple, iconv.
- Fixed -e option, when tarantool always entered interactive mode
  when stdin is a tty.
- Make recovering with force_recovery option delete newer than
  snapshot vylog files.

* Wed Dec 30 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.7.1.0-1
- LuaJIT memory profiler.
- Expression evaluation for replication_synchro_quorum.
- The ALTER TABLE ADD COLUMN statement.
- Many other features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.7.1

* Wed Dec 30 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.6.2.0-1
- The new box.ctl.is_recovery_finished() function allows user
  to determine whether memtx recovery is finished.
- It is now possible to specify synchro quorum as a function of
  a number N of registered replicas.
- Show JSON tokens themselves instead of token names T_*
  in the JSON decoder error messages.
- Show a decoding context in the JSON decoder error messages.
- Various bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.6.2

* Wed Dec 30 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.5.3.0-1
- The new box.ctl.is_recovery_finished() function allows user
  to determine whether memtx recovery is finished.
- A lot of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.5.3

* Wed Dec 30 2020 Kirill Yukhin <kyukhin@tarantool.org> 1.10.9.0-1
- Deploy packages for Debian Bullseye.
- Don't start an 'example' instance after installing tarantool.
- fiber.cond:wait() now correctly throws an error when
  a fiber is cancelled.
- Fixed a memory corruption issue.
- A dynamic module now gets correctly unloaded from memory in case
  of an attempt to load a non-existing function from it.
- The fiber region (the box region) won't be invalidated on
  a read-only transaction.
- Dispatching __call metamethod no longer causes address clashing.
- Fixed a false positive panic when yielding in debug hook.
- An attempt to use a net.box connection which is not established yet
  now results in a correctly reported error.
- Fixed a hang which occured when tarantool ran a user script with
  the -e option and this script exited with an error.

* Thu Oct 22 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.6.1.0-1
- Transactional manager for the memtx engine that allows yielding
  in transactions.
- Raft-based automated failover mechanism for a single-leader replica set.
- Many other features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.6.1

* Thu Oct 22 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.5.2.0-1
- New function space:alter(options).
- Exposed the box region, key_def and several other functions.
- A lot of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.5.2

* Thu Oct 22 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.4.3.0-1
- Exposed the box region, key_def and several other functions
  in order to implement external tuple.keydef and tuple.merger
  modules on top of them.
- Various improvements and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.4.3

* Thu Oct 22 2020 Kirill Yukhin <kyukhin@tarantool.org> 1.10.8.0-1
- Exposed the box region, key_def and several other functions in order
  to implement external tuple.keydef and tuple.merger modules
  on top of them.
- Fixed fibers switch-over to prevent JIT machinery misbehavior.
- Fixed fibers switch-over to prevent implicit GC disabling.
- Fixed unhandled Lua error that might lead to memory leaks and
  inconsistencies in <space_object>:frommap(), <key_def_object>:compare(),
  <merge_source>:select().
- Fixed the error occurring on loading luajit-gdb.py with Python2.
- Fixed potential lag on boot up procedure when system's password
  database is slow in access.
- Get rid of typedef redefinitions for compatibility with C99.

* Fri Jul 17 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.5.1.0-1
- Synchronous replication (beta).
- Allow an anonymous replica follow another anonymous replica.
- Fixed numerous crashes in Vinyl.
- Make implicit cast rules for assignment operation more strict in SQL.
- Updated curl version to 7.71.
- Don't start 'example' instance after installing tarantool.
- Many other features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.5.1

* Fri Jul 17 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.4.2.0-1
- box.session.push() parameter sync is deprecated.
- Don't start 'example' instance after installing tarantool.
- Various bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.4.2

* Fri Jul 17 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.3.3.0-1
- Various improvements and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.3.3

* Fri Jul 17 2020 Kirill Yukhin <kyukhin@tarantool.org> 1.10.7.0-1
- Fixed a bug in C module reloading.
- Fixed races and corner cases in box (re)configuration.
- Fixed check of index field map size which led to crash.
- Fixed wrong mpsgpack extension type in an error message at decoding.
- Fixed error while closing socket.tcp_server socket.
- Don't ruin rock name when freshly installing *.all.rock
- with dependencies.
- Fixed crash during compaction due to tuples with size exceeding
  vinyl_max_tuple_size setting.
- Fixed crash during recovery of vinyl index due to the lack of file
  descriptor.
- Fixed crash during executing upsert changing primary key
  in debug mode.
- Fixed crash due to triggered dump process during secondary index
  creation.
- Fixed crash/deadlock (depending on build type) during dump process
  scheduling and concurrent DDL operation.
- Fixed crash during read of prepared but still not yet
  not committed statement.
- Fixed squashing set and arithmetic upsert operations.
- Create missing folders for vinyl spaces and indexes if needed
  to avoid confusing fails of tarantool started from backup.
- Fixed crash during squash of many (more than 4000) upserts modifying
  the same key.

* Mon Apr 20 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.4.1.0-1
- UUID type was introduced.
- It is now possible to report stack of errors.
- Added popen built-in module.
- Create errors of custom type and transparent marshaling over net.box.
- Many other features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.4.1

* Mon Apr 20 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.3.2.0-1
- fiber.storage is cleaned between requests.
- tuple/space/index:update()/upsert() were fixed not to turn
  a value into an infinity when a float value was added to or
  subtracted from a float column and exceeded the float value range
- Fix potential execution abort when operating the system runs
  under heavy memory load.
- Make RTREE indexes handle the out of memory error.
- Fix the error message returned on using an already dropped sequence.
- Add cancellation guard to avoid WAL thread stuck.
- Fix execution abort when memtx_memory and vinyl_memory are set
  to more than 4398046510080 bytes.
- Fix rebootstrap procedure not working in case replica itself is
  listed in box.cfg.replication.
- Fix possible user password leaking via replication logs.
- Refactor vclock map to be exactly 4 bytes in size.
- Fix crash when the replication applier rollbacks a transaction.
- Local space operations are now counted in 0th vclock component.
- Gc consumers are now ordered by their vclocks and not by vclock
  signatures.
- json:decode() doesn't spoil instance's options with per-call ones.
- Handle empty input for uri.format() properly.
- os.environ() is now changed when os.setenv() is called.
- netbox.self:call/eval() now returns the same types as
  netbox_connection:call/eval.
- box.tuple.* namespace is cleaned up from private functions.
- tarantoolctl rocks search: fix the --all flag.
- tarantoolctl rocks remove: fix the --force flag.
- Fix box.stat() behavior: now it collects statistics on the
  PREPARE and EXECUTE methods as expected.
- The inserted values are inserted in the order in which they are given
  in case of SQL INSERT into space with autoincrement.
- When building Tarantool with bundled libcurl, link it with the c-ares
  library by default.
- __pairs/__ipairs metamethods handling is removed.
- Introduce luajit-gdb.py extension with commands for inspecting
  LuaJIT internals.
- Fix string to number conversion.
- "FFI sandwich"(*) detection is introduced.
- luaJIT_setmode call is prohibited while mcode execution.
- Fix assertion fault due to triggered dump process during
  secondary index build.
- Fix crashes at attempts to use -e and -l command line options
  concatenated with their values.
- Fix inability to upgrade from 2.1 if there was an automatically
  generated sequence.
- Update libopenssl version to 1.1.1f since the previous one was EOLed.
- Fix static build (-DBUILD_STATIC=ON) when libunwind depends on liblzma.

* Mon Apr 20 2020 Kirill Yukhin <kyukhin@tarantool.org> 2.2.3.0-1
- Various improvements and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.2.3

* Mon Apr 20 2020 Kirill Yukhin <kyukhin@tarantool.org> 1.10.6.0-1
- fiber.storage is cleaned between requests.
- tuple/space/index:update()/upsert() were fixed not to turn a value
  into an infinity when a float value was added to or subtracted from
  a float column and exceeded the float value range.
- Make RTREE indexes handle the out of memory error.
- Add cancellation guard to avoid WAL thread stuck.
- Fix the rebootstrap procedure not working if the replica itself
  is listed in box.cfg.replication.
- Fix possible user password leaking via replication logs.
- Local space operations are now counted in 0th vclock component.
- Gc consumers are now ordered by their vclocks and not by vclock
  signatures.
- json: :decode() doesn't spoil instance's options with per-call ones.
- os.environ() is now changed when os.setenv() is called.
- netbox.self:call/eval() now returns the same types as
  netbox_connection:call/eval.
- __pairs/__ipairs metamethods handling is removed.
- Introduce luajit-gdb.py extension with commands for inspecting
  LuaJIT internals.
- Fix string to number conversion.
- "FFI sandwich" detection is introduced.
- luaJIT_setmode call is prohibited while mcode execution and leads
  to the platform panic.
- Fix assertion fault due to triggered dump process during secondary
  index build.
- Fix crashes at attempts to use -e and -l command line options
  concatenated with their values.
- Update libopenssl version to 1.1.1f.

* Tue Jan 14 2020 Kirill Yukhin <kyukhin@tarantool.org> 1.10.5.0-1
- Exit gracefully when a main script throws an error.
- Enable __pairs and __ipairs metamethods from Lua 5.2.
- A lof of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/1.10.5

* Tue Dec 31 2019 Alexander Turenko <alexander.turenko@tarantool.org> 2.3.1.0-1
- Field name and JSON path updates.
- Anonymous replica.
- New DOUBLE SQL type (and new 'double' box field type).
- Stored and indexed decimals (and new 'decimal' field type).
- fiber.top()
- Feed data from a memory during replica initial join.
- SQL prepared statements.
- Sessions settings service space.
- Many other features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.3.1

* Tue Dec 31 2019 Alexander Turenko <alexander.turenko@tarantool.org> 2.2.2.0-1
- Drop rows_per_wal box.cfg() option in favor of wal_max_size.
- json and msgpack serializers now raise an error when a depth of
  data nesting exceeds encode_max_depth option value.
- Show line and column in json.decode() errors.
- Exit gracefully when a main script throws an error.
- key_def: accept both field and fieldno in key_def.new(<...>).
- Enable __pairs and __ipairs metamethods from Lua 5.2.
- tarantoolctl: allow to start instances with delayed box.cfg{}.
- Dozens of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.2.2

* Wed Nov 06 2019 Kirill Yukhin <kyukhin@tarantool.org> 2.1.3.0-1
- Various improvements and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.1.3

* Thu Sep 26 2019 Kirill Yukhin <kyukhin@tarantool.org> 1.10.4.0-1
- Improve dump start/stop logging.
- Look up key in reader thread.
- Improve box.stat.net.
- Add idle to downstream status in box.info.
- Deprecate rows_per_wal in favor of wal_max_size.
- Print corrupted rows on decoding error.
- Add type of operation to space trigger parameters.
- Add debug.sourcefile() and debug.sourcedir() helpers to determine
  the location of a current Lua source file.
- Add max_total_connections option in addition to total_connection
  to allow more fine-grained tuning of libcurl connection cache.
- A lof of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/1.10.4

* Fri Aug 02 2019 Kirill Yukhin <kyukhin@tarantool.org> 2.2.1.0-1
- New fixed point type (DECIMAL) was introduced to Tarantool.
- Multikey index support.
- Now it is possible to make functions persistent.
- Functional indexes implemented.
- Partial core dumps, which are now on by default.
- Data definition statements, which do not yield, can now be used
  in a transaction.
- It is now possible to set a sequence not only for the first part
  of the index.
- Allow to call box.session.exists() and box.session.fd() without
  any arguments.
- New function introduced to get index key from tuple.
- New protocol called SWIM implemented to keep a table
  of cluster members.
- Provide type of operation inside before_replace() trigger.
- Remove yields from Vinyl DDL on commit triggers.
- Improved performance of SELECT-s on memtx spaces.
- Indexes of memtx spaces are now built in background fibers.
- Hand over key lookup in a page to vinyl reader thread.
- Replication applier now can apply transactions which were
  concurrent on the master concurrently on replica.
- Transaction boundaries introduced to replication protocol.
- Tuple access by field name for net.box.
- It is now possible to set the output format to Lua instead of YAML
  in the interactive console.
- Multiple new collations added.
- New function touch introduced to fio module.
- Merger for tuples streams added.
- Default collation strength is explicit tertiary now.
- Improve box.stat.net.
- Tarantool now uses luarocks 3.
- New module key_def introduced to Lua.
- SQL ALTER now allows to add a constraint.
- SQL CHECK constraints are also validated during DML operations
  performed from Lua land.
- New SQL types VARBINARY, UNSIGNED and BOOLEAN.
- CREATE TABLE SQL statement (and all other data definition statements)
  are now truly transactional.
- SQL now uses Tarantool diagnostics API to set errors.
- Multiple improvements to SQL type system to make it more consistent.
- Added aliases for LENGTH() from ANSI SQL.
- It is possible to use HAVING without GROUP BY in SQL.
- A lot of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.2.1

* Fri Apr 05 2019 Kirill Yukhin <kyukhin@tarantool.org> 2.1.2.0-1
- Index based on JSON path.
- In case of ENOSPC delete WALs not needed for recovery
  from the last checkpoint.
- Add support for "tarantoolctl rocks pack" subcommand.
- string.rstrip and string.lstrip should accept symbols to strip.
- on shutdown trigger added.
- on_schema_init trigger added.
- snapshot daemon: limit the maximum disk size of maintained WALs.
- Permissions to create, alter and drop space.
- Replace box.sql.execute() with box.execute().
- SQL Type system was significantly refactored.
- Do not use SQL delete+insert for updates.
- Improve value comparison of different SQL types.
- Allow constraints to appear along with column definitions in SQL.
- Improve pragma index_info syntax.
- Dozens of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.1.2

* Mon Apr 1 2019 Kirill Yukhin <kyukhin@tarantool.org> 1.10.3.0-1
- Randomize vinyl index compaction.
- Throttle tx thread if compaction doesn't keep up with dumps.
- Do not apply run_count_per_level to the last level.
- Report the number of active iproto connections.
- Never keep a dead replica around if running out of disk space.
- Report join progress to the replica log.
- Expose snapshot status in box.info.gc().
- Show names of Lua functions in backtraces in fiber.info().
- Check if transaction opened.
- A lof of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/1.10.3

* Thu Nov 15 2018 Kirill Yukhin <kyukhin@tarantool.org> 2.1.1.0-1
- Add function box.sql.execute() to query Tarantool database
  using SQL statements.
- Added support for SQL collations by incorporating libICU
  character set and collation library.
- IPROTO interface was extended to support SQL queries.
- net.box subsystem was extended to support SQL queries.
- Enabled ANALYZE statement to produce correct results,
  necessary for efficient query plans.
- Enabled savepoints functionality. SAVEPOINT statement works w/o issues.
- Enabled ALTER TABLE … RENAME statement.
- Improved rules for identifier names: now fully consistent
  with Lua frontend.
- Enabled support for triggers; trigger bodies now persist in Tarantool
  snapshots and survive server restart.
- Significant performance improvements.
- Functions in SQL.
- Introduce new SQL wire protocol.
- Is nullable type attribute wanted by API in IPROTO.
- Add collation field to tuple format.
- Add string.fromhex() method.
- Select input language in tarantoolctl.
- Use Tarantool's structs for metadata during query compilation.
- Resolve names for VIEW right after its creation.
- Various SQL features and bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/2.1.1

* Sat Oct 13 2018 Kirill Yukhin <kyukhin@tarantool.org> 1.10.2.0-1
- Configurable syslog destination.
- Allow different nullability in indexes and format.
- Allow to back up any checkpoint.
- A way to detect that a Tarantool process was started or
  restarted by tarantoolctl.
- `TARANTOOLCTL` and `TARANTOOL_RESTARTED` env vars.
- New configuration parameter net_msg_max to restrict
  the number of allocated fibers;
- Automatic replication rebootstrap.
- Replica-local space.
- Display the connection status if the downstream gets
  disconnected from the upstream.
- New option replication_skip_conflict.
- Remove old snapshots which are not needed by replicas.
- New function fiber.join().
- New option `names_only` in tuple:tomap().
- Support custom rock servers in tarantoolctl. 
- Expose on_commit/on_rollback triggers to Lua.
- New function box.is_in_txn() to check if a transaction is open.
- Tuple field access via a json path.
- New function space:frommap() to convert a map to a tuple instance
  or to a table.
- New module utf8 that implements libicu's bindings for use in Lua.
- Support ALTER for non-empty vinyl spaces.
- Tuples stored in the vinyl cache are not shared among the indexes
  of the same space.
- Keep a stack of UPSERTS in `vy_read_iterator`.
- New function `box.ctl.reset_stat()` to reset vinyl statistics.
- A lof of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/1.10.2

* Wed Sep 05 2018 Kirill Yukhin <kyukhin@tarantool.org> 1.9.2.0-1
- Dozens of bugfixes, see GitHub release notes:
  https://github.com/tarantool/tarantool/releases/tag/1.9.1
  https://github.com/tarantool/tarantool/releases/tag/1.9.2

* Mon Feb 26 2018 Konstantin Osipov <kostja@tarantool.org> 1.9.0.4-1
- It is now possible to block/unblock users.
- New function `box.session.euid()` to return effective user.
- New 'super' role, with superuser access.
- `on_auth` trigger is now fired in case of both successful and
  failed authentication.
- New replication configuration algorithm.
- After replication connect at startup, the server
  does not start processing write requests before syncing up
  with all connected peers.
- It is now possible to explicitly set instance and replica set
  uuid via database configuration.
- `box.once()` no longer fails on a read-only replica but waits.
- `force_recovery` can now skip a corrupted xlog file.
- Improved replication monitoring.
- New 'BEFORE' triggers which can be used for conflict resolution
  in master-master replication.
- http client now correctly parses cookies and supports
  http+unix:// paths.
- `fio` rock now supports `file_exists()`, `rename()` works across
  filesystems, and `read()` without arguments reads the whole file.
- `fio` rock errors now follow Tarantool function call conventions
  and always return an error message in addition to the error flag.
- `digest` rock now supports pbkdf2 password hashing algorithm,
  useful in PCI/DSS compliant applications.
- `box.info.memory()` provides a high-level overview of
  server memory usage, including networking, Lua, transaction
  and index memory.
- It is now possible to add missing tuple fields to an index.
- Lots of improvements in field type support when creating or
  altering spaces and indexes.
- It is now possible to turn on `is_nullable` property on a field
  even if the space is not empty.
- Several logging improvements. 
- It is now possible to make a unique vinyl index non-unique
  without index rebuild.
- Improved vynil UPDATE, REPLACE and recovery performance in presence of
  secondary keys.
- `space:len()` and `space:bsize()` now work for vinyl.
- Vinyl recovery speed has improved in presence of secondary keys.

* Tue Nov 07 2017 Roman Tsisyk <roman@tarantool.org> 1.7.6.0-1
- Hybrid schema-less + schema-full data model.
- Collation and Unicode Support.
- NULL values in unique and non-unique indexes.
- Sequences and a new implementation of auto_increment().
- Add gap locks in Vinyl transaction manager.
- on_connect/on_disconnect triggers for net.box.
- Structured logging in JSON format.
- Several Lua features and various bugfixes, see GitHub release notes
  for details: https://github.com/tarantool/tarantool/releases/tag/1.7.6

* Tue Sep 12 2017 Roman Tsisyk <roman@tarantool.org> 1.7.5.46-1
- Stabilization of Vinyl storage engine.
- Improved MemTX TREE iterators.
- Better replication monitoring.
- WAL tracking for remote replicas on master.
- Automatic checkpoints every hour.
- Lua API to create consistent backups.
- Hot code reload for stored C procedures.
- New built-in rocks: 'http.client', 'iconv' and 'pwd'.
- Lua 5.1 command line options.
- LuaRocks-based package manager.
- Stack traces support in fiber.info().
- New names for box.cfg() options.
- Hot standy mode is now off by default.
- Support for UNIX pipes in tarantoolctl.
- Non-blocking syslog logger.
- Improved systemd integration.
- Hundrends of bugs fixed, see GitHub release notes for details:
  https://github.com/tarantool/tarantool/releases/tag/1.7.5

* Fri Dec 16 2016 Roman Tsisyk <roman@tarantool.org> 1.7.2.385-1
- Add `tarantoolctl cat/play` commands and `xlog` Lua module.
- Add Lua library to manipulate environment variables.
- Allow DML requests from on_replace triggers.
- Allow UPSERT without operations.
- Improve support for the large number of active network clients.
- Fix race conditions during automatic cluster bootstrap.
- Fix calculation of periods in snapshot daemon.
- Fix handling of iterator type in space:pairs() and space:select().
- Fix CVE-2016-9036 and CVE-2016-9037.
- Dozens of bug fixes to Vinyl storage engine.
- Remove broken coredump() Lua function.

* Thu Sep 29 2016 Roman Tsisyk <roman@tarantool.org> 1.7.2.1-1
- Vinyl - a new write-optimized storage engine, allowing the amount of
  data stored to exceed the amount of available RAM 10-100x times.
- A new binary protocol command for CALL, which no more restricts a function
  to returning an array of tuples and allows returning an arbitrary
  MsgPack/JSON result, including scalars, nil and void (nothing).
- Automatic replication cluster bootstrap; it's now much easier to configure
  a new replication cluster.
- New indexable data types: unsigned, integer, number and scalar.
- memtx snapshots and xlog files are now compressed on the fly using the
  fast ZStandard compression algorithm. Compression options are configured
  automatically to get an optimal trade-off between CPU utilization and disk
  throughput.
- fiber.cond() - a new synchronization mechanism for fibers.
- Tab-based autocompletion in the interactive console.
- A new implementation of net.box improving performance and solving
  problems with the garbage collection of dead connections.
- Native systemd integration alongside sysvinit.
- A ready-to-use 'example.lua' instance enable by default.
- Dozens of bugfixes:
  https://github.com/tarantool/tarantool/issues?q=milestone%3A1.7.2+is%3Aclosed

* Wed Sep 28 2016 Roman Tsisyk <roman@tarantool.org> 1.6.9.6-1
- Add dependency on network configuration files used by `socket` module

* Mon Sep 26 2016 Roman Tsisyk <roman@tarantool.org> 1.6.9.1-1
- Tab-based autocompletion in the interactive console
- LUA_PATH and LUA_CPATH environment variables taken into account
- A new box.cfg { read_only = true } option
- Upgrade script for 1.6.4 -> 1.6.8 -> 1.6.9
- Bugs fixed:
  https://github.com/tarantool/tarantool/issues?q=milestone%3A1.6.9+is%3Aclosed

* Thu Sep 01 2016 Roman Tsisyk <roman@tarantool.org> 1.6.8.762-1
- Add support for OpenSSL 1.1

* Tue Feb 09 2016 Roman Tsisyk <roman@tarantool.org> 1.6.8.462-1
- Enable tests

* Fri Feb 05 2016 Roman Tsisyk <roman@tarantool.org> 1.6.8.451-1
- Add coreutils, make and sed to BuildRequires

* Wed Feb 03 2016 Roman Tsisyk <roman@tarantool.org> 1.6.8.433-1
- Obsolete tarantool-common package

* Thu Jan 21 2016 Roman Tsisyk <roman@tarantool.org> 1.6.8.376-1
- Implement proper support of multi-instance management using systemd

* Sat Jan 9 2016 Roman Tsisyk <roman@tarantool.org> 1.6.8.0-1
- Change naming scheme to include a postrelease number to Version
- Fix arch-specific paths in tarantool-common
- Rename tarantool-dev to tarantool-devel
- Use system libyaml
- Remove Vendor
- Remove SCL support
- Remove devtoolkit support
- Remove Lua scripts
- Remove quotes from %%files
- Disable hardening to fix backtraces
- Fix permissions for tarantoolctl directories
- Comply with http://fedoraproject.org/wiki/Packaging:DistTag
- Comply with http://fedoraproject.org/wiki/Packaging:LicensingGuidelines
- Comply with http://fedoraproject.org/wiki/Packaging:Tmpfiles.d
- Comply with the policy for log files
- Comply with the policy for man pages
- Other changes according to #1293100 review

* Tue Apr 28 2015 Roman Tsisyk <roman@tarantool.org> 1.6.5.0-1
- Remove sql-module, pg-module, mysql-module

* Fri Jun 06 2014 Eugine Blikh <bigbes@tarantool.org> 1.6.3.0-1
- Add SCL support
- Add --with support
- Add dependencies

* Mon May 20 2013 Dmitry Simonenko <support@tarantool.org> 1.5.1.0-1
- Initial version of the RPM spec
