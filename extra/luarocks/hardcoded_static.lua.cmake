local fio = require('fio')

local curdir = fio.cwd()
local bindir = fio.dirname(arg[-1])

-- Use paths relative to <dir> if Tarantool binary is located in <dir>/bin,
-- otherwise use paths relative to the binary directory.
local prefix
if fio.basename(bindir) == 'bin' then
    prefix = fio.dirname(bindir)
else
    prefix = bindir
end

local cfg = {
    PREFIX = prefix,
    LUA_INCDIR = fio.pathjoin(prefix, 'include', 'tarantool'),
    LUA_BINDIR = bindir,
    LUA_INTERPRETER = [[tarantool]],
    LUA_MODULES_LIB_SUBDIR = [[/lib/tarantool]],
    LUA_MODULES_LUA_SUBDIR = [[/share/tarantool]],
    SYSCONFDIR = fio.pathjoin(prefix, 'etc', 'tarantool', 'rocks'),
    FORCE_CONFIG = false,
    ROCKS_SUBDIR = [[/share/tarantool/rocks]],
    ROCKS_SERVERS = {
        [[http://rocks.tarantool.org/]],
    },
    LOCALDIR = fio.cwd(),
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

-- Search local rocks tree first if one is present.
if fio.path.is_file(fio.pathjoin(prefix, 'rocks', 'manifest')) then
    table.insert(cfg.ROCKS_SERVERS, 1,
                 'file://' .. fio.pathjoin(prefix, 'rocks'))
end

return cfg
