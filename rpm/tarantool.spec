# Enable systemd for on RHEL >= 7 and Fedora >= 15
%if (0%{?fedora} >= 15 || 0%{?rhel} >= 7 || 0%{?sle_version} >= 1500)
%bcond_without systemd
%else
%bcond_with systemd
%endif

# XXX: There is an old CMake (2.8.12) provided by cmake package in
# main CentOS 7 repositories. At the same time, there is a newer
# package cmake3 providing CMake 3+ from EPEL repository. So, one
# need to use cmake3 package to build Tarantool on old systems.
%define use_cmake3 0%{?rhel} == 7

%if %use_cmake3
# XXX: Unfortunately there is no way to make rpmbuild install and
# enable EPEL repository prior to the build step. However, the
# requirement below obligues user to enable EPEL by himself,
# otherwise this dependency is left unmet. If there are any issues
# with building an RPM package on RHEL/CentOS 7 read the docs:
# https://www.tarantool.io/en/doc/latest/dev_guide/building_from_source/
BuildRequires: cmake3 >= 3.2
%else
BuildRequires: cmake >= 3.2
%endif

BuildRequires: make
%if (0%{?fedora} >= 22 || 0%{?rhel} >= 7 || 0%{?sle_version} >= 1500)
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

%if %{with systemd}
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd
BuildRequires: systemd
%else
Requires(post): chkconfig
Requires(post): initscripts
Requires(preun): chkconfig
Requires(preun): initscripts
%endif

%if 0%{?rhel} >= 8
# gh-4611: Disable backtraces on CentOS 8 by default due to lack
# of libunwind package in the base system.
%bcond_with backtrace
%else
# Enable backtraces by default.
%bcond_without backtrace
%endif

# openSuSE sets its own build directory in its macros, but we
# want to use in-source build there to simplify the RPM spec.
%if (0%{?sle_version} >= 1500)
%global __builddir .
%endif

%if %{with backtrace}
BuildRequires: libunwind-devel
#
# Disable stripping of /usr/bin/tarantool to allow the debug symbols
# in runtime. Tarantool uses the debug symbols to display fiber's stack
# traces in fiber.info().
#
%global _enable_debug_package 0
%global debug_package %{nil}
%global __os_install_post /usr/lib/rpm/brp-compress %{nil}
%global __strip /bin/true
# -fPIE break backtraces
# https://github.com/tarantool/tarantool/issues/1262
%undefine _hardened_build
%endif

# Set dependences for tests.
BuildRequires: python3
BuildRequires: python3-six
BuildRequires: python3-gevent
%if (0%{?sle_version} >= 1500)
BuildRequires: python3-PyYAML
%else
BuildRequires: python3-pyyaml
%endif

# Install prove to run LuaJIT tests.
BuildRequires: perl-Test-Harness

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
%if (0%{?fedora} >= 22 || 0%{?rhel} >= 8 || 0%{?sle_version} >= 1500)
# RHEL <= 7 doesn't support Recommends:
Recommends: tarantool-devel
Recommends: git-core
Recommends: cmake >= 3.2
Recommends: make
Recommends: gcc >= 4.5
Recommends: gcc-c++ >= 4.5
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
%if %{with backtrace}
         -DENABLE_BACKTRACE:BOOL=ON \
%else
         -DENABLE_BACKTRACE:BOOL=OFF \
%endif
%if %{with systemd}
         -DWITH_SYSTEMD:BOOL=ON \
         -DSYSTEMD_UNIT_DIR:PATH=%{_unitdir} \
         -DSYSTEMD_TMPFILES_DIR:PATH=%{_tmpfilesdir} \
%endif
%if 0%{?fedora} >= 33
         -DENABLE_LTO=ON \
%endif
         -DENABLE_WERROR:BOOL=ON \
         -DENABLE_DIST:BOOL=ON
make %{?_smp_mflags}

%install
%make_install
# %%doc and %%license macroses are used instead
rm -rf %{buildroot}%{_datarootdir}/doc/tarantool/

%check
make test-force

%pre
/usr/sbin/groupadd -r tarantool > /dev/null 2>&1 || :
/usr/sbin/useradd -M %{?rhel:-N} -g tarantool -r -d /var/lib/tarantool -s /sbin/nologin\
    -c "Tarantool Server" tarantool > /dev/null 2>&1 || :

%post
%if %{with systemd}
%tmpfiles_create tarantool.conf
%systemd_post tarantool@.service
%else
chkconfig --add tarantool || :
%endif

%preun
%if %{with systemd}
%systemd_preun tarantool@.service
%else
if [ $1 -eq 0 ] ; then # uninstall
    service tarantool stop || :
    chkconfig --del tarantool || :
fi
%endif

%postun
%if %{with systemd}
%systemd_postun_with_restart tarantool@.service
%endif

%files
%{_bindir}/tarantool
%{_mandir}/man1/tarantool.1*
%doc README.md
%{!?_licensedir:%global license %doc}
%license LICENSE AUTHORS

%{_bindir}/tarantoolctl
%{_mandir}/man1/tarantoolctl.1*
%config(noreplace) %{_sysconfdir}/sysconfig/tarantool
%dir %{_sysconfdir}/tarantool
%dir %{_sysconfdir}/tarantool/instances.available
%config(noreplace) %{_sysconfdir}/tarantool/instances.available/example.lua
# Use 0750 for database files
%attr(0750,tarantool,tarantool) %dir %{_localstatedir}/lib/tarantool/
%attr(0750,tarantool,tarantool) %dir %{_localstatedir}/log/tarantool/
%config(noreplace) %{_sysconfdir}/logrotate.d/tarantool
# tarantool package should own module directories
%dir %{_libdir}/tarantool
%dir %{_datadir}/tarantool
%{_datadir}/tarantool/luarocks

%if %{with systemd}
%{_unitdir}/tarantool@.service
%{_tmpfilesdir}/tarantool.conf
%else
%{_sysconfdir}/init.d/tarantool
%dir %{_sysconfdir}/tarantool/instances.enabled
%attr(-,tarantool,tarantool) %dir %{_localstatedir}/run/tarantool/
%endif

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
