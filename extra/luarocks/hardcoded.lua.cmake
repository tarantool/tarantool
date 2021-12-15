return {
    PREFIX = [[@CMAKE_INSTALL_PREFIX@]],
    LUA_INCDIR = [[@MODULE_FULL_INCLUDEDIR@]],
    LUA_BINDIR = [[@CMAKE_INSTALL_FULL_BINDIR@]],
    LUA_INTERPRETER = [[tarantool]],
    LUA_MODULES_LIB_SUBDIR = [[/lib/tarantool]],
    LUA_MODULES_LUA_SUBDIR = [[/share/tarantool]],
    SYSCONFDIR = [[@CMAKE_INSTALL_FULL_SYSCONFDIR@/tarantool/rocks]],
    FORCE_CONFIG = false,
    ROCKS_SUBDIR = [[/share/tarantool/rocks]],
    ROCKS_SERVERS = {
        [[http://rocks.tarantool.org/]],
    },
    LOCALDIR = require('fio').cwd(),
    HOME_TREE_SUBDIR = [[/.rocks]],
    EXTERNAL_DEPS_SUBDIRS = {
        bin = "bin",
        lib = {"lib", [[@MULTILIB@]]},
        include = "include",
    },
    RUNTIME_EXTERNAL_DEPS_SUBDIRS = {
        bin = "bin",
        lib = {"lib", [[@MULTILIB@]]},
        include = "include",
    },
    LOCAL_BY_DEFAULT = true,
}
