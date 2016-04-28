# Enable systemd for on RHEL >= 7 and Fedora >= 15
%if (0%{?fedora} >= 15 || 0%{?rhel} >= 7)
%bcond_without systemd
%else
%bcond_with systemd
%endif

BuildRequires: cmake >= 2.8
BuildRequires: gcc >= 4.5
BuildRequires: coreutils
BuildRequires: sed
BuildRequires: readline-devel
BuildRequires: libyaml-devel
BuildRequires: openssl-devel
%if 0%{?fedora} >= 22
BuildRequires: lz4-devel >= r131
%endif
#BuildRequires: msgpuck-devel
%if 0%{?fedora} > 0
# pod2man is needed to build man pages
BuildRequires: perl-podlators
%endif
Requires(pre): %{_sbindir}/useradd
Requires(pre): %{_sbindir}/groupadd

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

%bcond_without backtrace # enabled by default

%if %{with backtrace}
# binutils and zlib are needed for stack traces in fiber.info()
BuildRequires: binutils-devel
BuildRequires: zlib-devel
#
# Disable stripping of /usr/bin/tarantool to allow the debug symbols
# in runtime. Tarantool uses the debug symbols to display fiber's stack
# traces in fiber.info().
#
%global debug_package %%{nil}
# -fPIE break backtraces
# https://github.com/tarantool/tarantool/issues/1262
%undefine _hardened_build
%endif

# For tests
%if (0%{?fedora} >= 22 || 0%{?rhel} >= 7)
BuildRequires: python >= 2.7
BuildRequires: python-six >= 1.9.0
BuildRequires: python-gevent >= 1.0
BuildRequires: python-yaml >= 3.0.9
%endif

Name: tarantool
# ${major}.${major}.${minor}.${patch}, e.g. 1.6.8.175
# Version is updated automaically using git describe --long --always
Version: 1.6.8.0
Release: 1%{?dist}
Group: Applications/Databases
Summary: In-memory database and Lua application server
License: BSD

Provides: tarantool-debuginfo = %{version}-%{release}
Provides: tarantool-common = %{version}-%{release}
Obsoletes: tarantool-common < 1.6.8.434-1
URL: http://tarantool.org
Source0: http://tarantool.org/dist/1.6/tarantool-%{version}.tar.gz
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
%cmake . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
         -DCMAKE_INSTALL_LOCALSTATEDIR:PATH=%{_localstatedir} \
         -DCMAKE_INSTALL_SYSCONFDIR:PATH=%{_sysconfdir} \
         -DENABLE_BUNDLED_LIBYAML:BOOL=OFF \
%if 0%{?fedora} >= 22
         -DENABLE_BUNDLED_LZ4:BOOL=OFF \
%endif
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
         -DENABLE_DIST:BOOL=ON
make %{?_smp_mflags}

%install
%make_install
# %%doc and %%license macroses are used instead
rm -rf %{buildroot}%{_datarootdir}/doc/tarantool/

%check
%if (0%{?fedora} >= 22 || 0%{?rhel} >= 7)
# https://github.com/tarantool/tarantool/issues/1227
echo "self.skip = True" > ./test/app/socket.skipcond
# https://github.com/tarantool/tarantool/issues/1322
echo "self.skip = True" > ./test/app/digest.skipcond
# run a safe subset of the test suite
cd test && ./test-run.py unit/ app/ app-tap/ box/ box-tap/ engine/ phia/
%endif

%pre
/usr/sbin/groupadd -r tarantool > /dev/null 2>&1 || :
%if 0%{?rhel} < 6
/usr/sbin/useradd -M -g tarantool -r -d /var/lib/tarantool -s /sbin/nologin\
    -c "Tarantool Server" tarantool > /dev/null 2>&1 || :
%else
/usr/sbin/useradd -M -N -g tarantool -r -d /var/lib/tarantool -s /sbin/nologin\
    -c "Tarantool Server" tarantool > /dev/null 2>&1 || :
%endif

%post
%if %{with systemd}
%tmpfiles_create tarantool.conf
%systemd_post tarantool@.service
%else
chkconfig --add tarantool || :
service tarantool start || :
%endif

%preun
%if %{with systemd}
%systemd_preun tarantool@.service
%else
service tarantool stop
chkconfig --del tarantool
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
%{_includedir}/tarantool/luaconf.h
%{_includedir}/tarantool/lua.h
%{_includedir}/tarantool/lua.hpp
%{_includedir}/tarantool/luajit.h
%{_includedir}/tarantool/lualib.h
%{_includedir}/tarantool/module.h

%changelog
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
