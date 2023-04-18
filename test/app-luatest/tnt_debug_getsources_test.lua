local t = require('luatest')
local fio = require('fio')

local g = t.group()

local function readfile(filename)
    local f = assert(io.open(filename, "rb"))
    return f:read('*a')
end

local files = {
    'strict',
    'debug',
    'errno',
    'fiber',
    'env',
    'datetime',
    'box/session',
    'box/tuple',
    'box/key_def',
    'box/schema',
    'box/xlog',
    'box/net_box',
    'box/console',
    'box/merger',
    'third_party/checks/checks/version',
    'third_party/checks/checks',
    'third_party/metrics/metrics/api',
    'third_party/metrics/metrics/cartridge/failover',
    'third_party/metrics/metrics/cartridge/issues',
    'third_party/metrics/metrics/cfg',
    'third_party/metrics/metrics/collectors/counter',
    'third_party/metrics/metrics/collectors/gauge',
    'third_party/metrics/metrics/collectors/histogram',
    'third_party/metrics/metrics/collectors/shared',
    'third_party/metrics/metrics/collectors/summary',
    'third_party/metrics/metrics/const',
    'third_party/metrics/metrics/http_middleware',
    'third_party/metrics/metrics/init',
    'third_party/metrics/metrics/plugins/graphite',
    'third_party/metrics/metrics/plugins/json',
    'third_party/metrics/metrics/plugins/prometheus',
    'third_party/metrics/metrics/psutils/cpu',
    'third_party/metrics/metrics/psutils/psutils_linux',
    'third_party/metrics/metrics/quantile',
    'third_party/metrics/metrics/registry',
    'third_party/metrics/metrics/stash',
    'third_party/metrics/metrics/tarantool/clock',
    'third_party/metrics/metrics/tarantool/cpu',
    'third_party/metrics/metrics/tarantool/event_loop',
    'third_party/metrics/metrics/tarantool/fibers',
    'third_party/metrics/metrics/tarantool/info',
    'third_party/metrics/metrics/tarantool/luajit',
    'third_party/metrics/metrics/tarantool/memory',
    'third_party/metrics/metrics/tarantool/memtx',
    'third_party/metrics/metrics/tarantool/network',
    'third_party/metrics/metrics/tarantool/operations',
    'third_party/metrics/metrics/tarantool/replicas',
    'third_party/metrics/metrics/tarantool/runtime',
    'third_party/metrics/metrics/tarantool/slab',
    'third_party/metrics/metrics/tarantool/spaces',
    'third_party/metrics/metrics/tarantool/system',
    'third_party/metrics/metrics/tarantool/vinyl',
    'third_party/metrics/metrics/tarantool',
    'third_party/metrics/metrics/utils',
    'third_party/metrics/metrics/version',
    'conf/conf/cluster_config',
    'conf/conf/conf_section_config',
    'conf/conf/conf_section_credentials',
    'conf/conf/conf_template',
    'conf/conf/helpers',
    'conf/conf/init',
    'conf/conf/instance',
    'conf/conf/instance_config',
    'conf/conf/instance_state',
    'conf/conf/mainloop',
    'conf/conf/source/env',
    'conf/conf/source/yaml_file',
    'conf/conf/utils/schema',
}

-- calculate reporsitory root using directory of a current
-- script, but get 2 directories above
local function repo_root()
    local myself = fio.abspath(debug.getinfo(1,'S').source:gsub("^@", ""))
    local root = fio.abspath(fio.dirname(myself) .. '/../..')
    return root
end

g.test_tarantool_debug_getsources = function()
    local git_root = repo_root()
    t.assert_is_not(git_root, nil)
    t.assert(fio.stat(git_root):is_dir())
    local lua_src_dir = fio.pathjoin(git_root, '/src/lua')
    t.assert_is_not(lua_src_dir, nil)
    t.assert(fio.stat(lua_src_dir):is_dir())
    local box_lua_dir = fio.pathjoin(git_root, '/src/box/lua')
    t.assert_is_not(box_lua_dir, nil)
    t.assert(fio.stat(box_lua_dir):is_dir())
    local third_party_dir = fio.pathjoin(git_root, '/third_party')
    t.assert_is_not(third_party_dir, nil)
    t.assert(fio.stat(third_party_dir):is_dir())

    local tnt = require('tarantool')
    t.assert_is_not(tnt, nil)
    local luadebug = tnt.debug
    t.assert_is_not(luadebug, nil)

    for _, file in pairs(files) do
        local box_prefix = 'box/'
        local third_party_prefix = 'third_party/'
        local path
        if file:match(box_prefix) ~= nil then
            path = ('%s/%s.lua'):format(box_lua_dir,
                                        file:sub(#box_prefix + 1, #file))
        elseif file:match(third_party_prefix) ~= nil then
            path = ('%s/%s.lua'):format(third_party_dir,
                                        file:sub(#third_party_prefix + 1,
                                                 #file))
        else
            path = ('%s/%s.lua'):format(lua_src_dir, file)
        end
        local text = readfile(path)
        t.assert_is_not(text, nil)
        local source = luadebug.getsources(file)
        t.assert_equals(source, text)
        source = luadebug.getsources(('@builtin/%s.lua'):format(file))
        t.assert_equals(source, text)
    end
end
