local site_config = {}
site_config.LUAROCKS_PREFIX=[[@CMAKE_INSTALL_PREFIX@]]
site_config.LUA_INCDIR=[[@MODULE_FULL_INCLUDEDIR@]]
site_config.LUA_BINDIR=[[@CMAKE_INSTALL_FULL_BINDIR@]]
site_config.LUA_INTERPRETER=[[tarantool]]
site_config.LUA_MODULES_LIB_SUBDIR=[[/lib/tarantool]]
site_config.LUA_MODULES_LUA_SUBDIR=[[/share/tarantool]]
site_config.LUAROCKS_SYSCONFDIR=[[@CMAKE_INSTALL_FULL_SYSCONFDIR@/tarantool/rocks]]
site_config.LUAROCKS_FORCE_CONFIG=true
site_config.LUAROCKS_ROCKS_TREE=[[/usr/local/]]
site_config.LUAROCKS_ROCKS_SUBDIR=[[/share/tarantool/rocks]]
site_config.LUAROCKS_ROCKS_SERVERS={
    [[http://rocks.tarantool.org/]]
};
site_config.LUAROCKS_HOMEDIR = require('fio').cwd()
site_config.LUAROCKS_HOME_TREE_SUBDIR=[[/.rocks]]
site_config.LUA_DIR_SET=true
site_config.LUAROCKS_UNAME_S=[[@CMAKE_SYSTEM_NAME@]]
site_config.LUAROCKS_UNAME_M=[[@CMAKE_SYSTEM_PROCESSOR@]]
site_config.LUAROCKS_DOWNLOADER=[[curl]]
site_config.LUAROCKS_MD5CHECKER=[[openssl]]
site_config.LUAROCKS_EXTERNAL_DEPS_SUBDIRS={ bin="bin", lib={ "lib", [[@MULTILIB@]] }, include="include" }
site_config.LUAROCKS_RUNTIME_EXTERNAL_DEPS_SUBDIRS={ bin="bin", lib={ "lib", [[@MULTILIB@]] }, include="include" }
site_config.LUAROCKS_LOCAL_BY_DEFAULT = true
return site_config
