####################################################
################# MACROS AND DEFAULTS ##############
####################################################

%{?scl:%global _scl_prefix /opt/tarantool}
%{?scl:%scl_package tarantool}

%define _source_filedigest_algorithm 0
%define _binary_filedigest_algorithm 0

%global debug_package %{nil}
%global _enable_debug_package %{nil}
%global __debug_install_post %{nil}
%global __debug_package %{nil}

Source1: VERSION
%global build_version %(( cat %{SOURCE1} || git describe --long) | sed "s/[0-9]*\.[0-9]*\.[0-9]*-//" | sed "s/-[a-z 0-9]*//")
%global git_hash %((cat %{SOURCE1} || git describe --long) | sed "s/.*-//")
%global prod_version %((cat %{SOURCE1} || git describe --long) | sed "s/-[0-9]*-.*//")

%if (0%{?fedora} >= 15 || 0%{?rhel} >= 7) && %{undefined _with_systemd}
%global _with_systemd 1
%endif

%bcond_with    systemd

BuildRequires: readline-devel

%if 0%{?rhel} < 7 && 0%{?rhel} > 0
BuildRequires: cmake28
BuildRequires: devtoolset-2-toolchain
BuildRequires: devtoolset-2-binutils-devel
%else
BuildRequires: cmake >= 2.8
BuildRequires: gcc >= 4.5
BuildRequires: binutils-devel
%endif

%if 0%{?fedora} > 0
BuildRequires: perl-podlators
%endif

Requires(pre): /usr/sbin/useradd
Requires(pre): /usr/sbin/groupadd
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

# Strange bug.
# Fix according to http://www.jethrocarr.com/2012/05/23/bad-packaging-habits/
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

Name: %{?scl_prefix}tarantool
Version: %{prod_version}
Release: %{build_version}
Group: Applications/Databases
Summary: Tarantool - a NoSQL database in a Lua script
Vendor: tarantool.org
License: BSD
Requires: readline
Provides: %{?scl_prefix}tarantool-debuginfo
Provides: %{?scl_prefix}tarantool-debug
Requires: %{?scl_prefix}tarantool-common
URL: http://tarantool.org
Source0: %{version}.tar.gz
%description
Tarantool is a high performance in-memory NoSQL database. It supports
replication, online backup, stored procedures in Lua.

This package provides the server daemon and administration
scripts.

# Tarantool dev spec
%package dev
Summary: Tarantool C connector and header files
Vendor: tarantool.org
Group: Applications/Databases
Requires: %{?scl_prefix}tarantool = %{version}-%{release}
%description dev
Tarantool is a high performance in-memory NoSQL database.
It supports replication, online backup, stored procedures in Lua.

This package provides Tarantool client libraries.

%package common
Summary: Tarantool common files
Vendor: tarantool.org
Group: Applications/Databases
Provides: %{?scl_prefix}tarantool-common
%if 0%{?rhel} != 5
BuildArch: noarch
%endif
Requires: %{?scl_prefix}tarantool
%description common
Tarantool is a high performance in-memory NoSQL database.
It supports replication, online backup, stored procedures in Lua.

This package provides common files

##################################################################

%prep
%setup -q -c tarantool-%{version}

%build
[ -d tarantool-%{version}-%{build_version}-%{git_hash}-src ] && cd tarantool-%{version}-%{build_version}-%{git_hash}-src
# https://fedoraproject.org/wiki/Packaging:RPMMacros

%{lua:
    local function is_rhel_old()
        local version = tonumber(rpm.expand('0%{?rhel}'))
        return (version < 7 and version > 0)
    end
    function wrap_with_toolset(cmd)
        local cmd = rpm.expand(cmd)
        local devtoolset = 'scl enable devtoolset-2 %q\n'
        if is_rhel_old() then
            return string.format(devtoolset, cmd)
        end
        return cmd
    end
    local function cmake_key_value(key, value)
        return " -D"..key.."="..value
    end
    local function dev_with (obj, flag)
        local status = "OFF"
        if tonumber(rpm.expand("%{with "..obj.."}")) ~= 0 then
            status = "ON"
        end
        return cmake_key_value(flag, status)
    end
    local function dev_with_kv (obj, key, value)
        if tonumber(rpm.expand("%{with "..obj.."}")) ~= 0 then
            return cmake_key_value(key, value)
        end
        return ""
    end
    local cmd = 'cmake'
    if is_rhel_old() then
        cmd = 'cmake28'
    end
    cmd = cmd .. ' . '
        .. cmake_key_value('CMAKE_BUILD_TYPE', 'RelWithDebInfo')
        .. cmake_key_value('ENABLE_BACKTRACE', 'ON')
        .. cmake_key_value('CMAKE_INSTALL_PREFIX', '%{_prefix}')
        .. cmake_key_value('CMAKE_INSTALL_BINDIR', '%{_bindir}')
        .. cmake_key_value('CMAKE_INSTALL_LIBDIR', '%{_libdir}')
        .. cmake_key_value('CMAKE_INSTALL_LIBEXECDIR', '%{_libexecdir}')
        .. cmake_key_value('CMAKE_INSTALL_SBINDIR', '%{_sbindir}')
        .. cmake_key_value('CMAKE_INSTALL_SHAREDSTATEDIR', '%{_sharedstatedir}')
        .. cmake_key_value('CMAKE_INSTALL_DATADIR', '%{_datadir}')
        .. cmake_key_value('CMAKE_INSTALL_INCLUDEDIR', '%{_includedir}')
        .. cmake_key_value('CMAKE_INSTALL_INFODIR', '%{_infodir}')
        .. cmake_key_value('CMAKE_INSTALL_MANDIR', '%{_mandir}')
        .. cmake_key_value('CMAKE_INSTALL_LOCALSTATEDIR', '%{_localstatedir}')
        .. ' %{!?scl:-DCMAKE_INSTALL_SYSCONFDIR=%{_sysconfdir}}'
        .. ' %{!?scl:-DENABLE_RPM=ON}'
        .. ' %{?scl:-DENABLE_RPM_SCL=ON}'
        .. dev_with('systemd', 'WITH_SYSTEMD')
        .. dev_with_kv('systemd', 'SYSTEMD_SERVICES_INSTALL_DIR', '%{_unitdir}')

    print(wrap_with_toolset(cmd))}
%{lua:print(wrap_with_toolset('make %{?_smp_mflags}\n'))}

%install
[ -d tarantool-%{version}-%{build_version}-%{git_hash}-src ] && cd tarantool-%{version}-%{build_version}-%{git_hash}-src
make VERBOSE=1 DESTDIR=%{buildroot} install

%pre
/usr/sbin/groupadd -r tarantool > /dev/null 2>&1 || :
%if 0%{?rhel} < 6
/usr/sbin/useradd -M -g tarantool -r -d /var/lib/tarantool -s /sbin/nologin\
    -c "Tarantool Server" tarantool > /dev/null 2>&1 || :
%else
/usr/sbin/useradd -M -N -g tarantool -r -d /var/lib/tarantool -s /sbin/nologin\
    -c "Tarantool Server" tarantool > /dev/null 2>&1 || :
%endif

%post common
mkdir -m 0755 -p %{_var}/run/tarantool/
chown tarantool:tarantool %{_var}/run/tarantool/
mkdir -m 0755 -p %{_var}/log/tarantool/
chown tarantool:tarantool %{_var}/log/tarantool/
mkdir -m 0755 -p %{_var}/lib/tarantool/
chown tarantool:tarantool %{_var}/lib/tarantool/
mkdir -m 0755 -p %{_sysconfdir}/tarantool/instances.enabled/
mkdir -m 0755 -p %{_sysconfdir}/tarantool/instances.available/

%if %{with systemd}
%systemd_post tarantool.service
%else
chkconfig --add tarantool
/sbin/service tarantool start
%endif

%preun common
%if %{with systemd}
%systemd_preun tarantool.service
%else
/sbin/service tarantool stop
chkconfig --del tarantool
%endif

%postun common
%if %{with systemd}
%systemd_postun_with_restart tarantool.service
%endif

%files
%defattr(-,root,root,-)

"%{_bindir}/tarantool"

%dir "%{_datadir}/doc/tarantool"
"%{_datadir}/doc/tarantool/README.md"
"%{_datadir}/doc/tarantool/LICENSE"

"%{_mandir}/man1/tarantool.1.gz"

%files dev
%defattr(-,root,root,-)
%dir "%{_includedir}/tarantool"
"%{_includedir}/tarantool/lauxlib.h"
"%{_includedir}/tarantool/luaconf.h"
"%{_includedir}/tarantool/lua.h"
"%{_includedir}/tarantool/lua.hpp"
"%{_includedir}/tarantool/luajit.h"
"%{_includedir}/tarantool/lualib.h"
"%{_includedir}/tarantool/module.h"

%files common
%defattr(-,root,root,-)
"%{_bindir}/tarantoolctl"
"%{_mandir}/man1/tarantoolctl.1.gz"
"%{_sysconfdir}/sysconfig/tarantool"
%dir "%{_sysconfdir}/tarantool"
%dir "%{_sysconfdir}/tarantool/instances.available"
"%{_sysconfdir}/tarantool/instances.available/example.lua"
%if %{with systemd}
%dir "%{_libdir}/tarantool/"
"%{_unitdir}/tarantool.service"
"%{_libdir}/tarantool/tarantool.init"
%else
"%{_sysconfdir}/init.d/tarantool"
%endif

%changelog
* Tue Apr 28 2015 roman@tarantool.org <roman@tarantool.org> 1.0-3
- Remove sql-module, pg-module, mysql-module
* Fri Jun 06 2014 Eugine Blikh <bigbes@tarantool.org> 1.0-2
- Add SCL support
- Add --with support
- Add dependencies
* Mon May 20 2013 Dmitry Simonenko <support@tarantool.org> 1.0-1
- Initial version of the RPM spec
