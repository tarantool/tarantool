local t = require('luatest')

local justrun = require('luatest.justrun')
local server = require('luatest.server')
local treegen = require('luatest.treegen')

local tests = {
    { chunk = [[print(debug.sourcefile())]] },
    { chunk = [[print(debug.sourcedir())]] },
    { chunk = [[print(debug.__file__)]] },
    { chunk = [[print(debug.__dir__)]] },
    { chunk = [[print(require('net.box').self:call('debug.sourcefile'))]] },
    { chunk = [[print(require('net.box').self:call('debug.sourcedir'))]] },
    { chunk = [[fn = function() return debug.__file__ end;
                print(require('net.box').self:call('fn'))]] },
    { chunk = [[fn = function() return debug.__dir__ end;
               print(require('net.box').self:call('fn'))]] },
    { chunk = [[fn = function() local res = debug.sourcefile(); return res end;
                print(require('net.box').self:call('fn'))]] },
    { chunk = [[fn = function() local res = debug.sourcedir(); return res end;
                print(require('net.box').self:call('fn'))]] },
    { chunk = [[print(loadstring('return debug.sourcefile()')())]] },
    { chunk = [[print(loadstring('return debug.sourcedir()')())]] },
    { chunk = [[print(loadstring('return debug.__file__')())]] },
    { chunk = [[print(loadstring('return debug.__dir__')())]] },
    { chunk = [[print(loadstring('local res = debug.sourcefile(); return res')())]] },
    { chunk = [[print(loadstring('local res = debug.sourcedir(); return res')())]] },
}

local g_remote = t.group('remote')

g_remote.before_all(function()
    g_remote.server = server:new({alias = 'remote_debug'})
    g_remote.server:start()
end)

g_remote.after_all(function()
    g_remote.server:stop()
end)

-- When running netbox call on remote server.
g_remote.test_debug_module_via_net_box = function()
    local NULL = require('json').NULL
    local conn = g_remote.server.net_box

    local result, err = conn:call('debug.sourcefile')
    t.assert_equals(result, NULL, 'debug.sourcefile() returns NULL')
    t.assert_equals(err, nil, 'debug.sourcefile() returns no error')

    local result, err = conn:call('debug.sourcedir')
    t.assert_equals(result, '.', 'debug.sourcedir() returns "."')
    t.assert_equals(err, nil, 'debug.sourcedir() returns no error')
end

local g = t.group('local', tests)

-- When running lua code from console.
-- Note, `debug.sourcefile()` returns cwd when running within
-- console.
g.test_debug_module_via_console = function(cg)
    local TARANTOOL_PATH = arg[-1]
    local cmd = string.format('%s -e "%s; os.exit(0)"',
                              TARANTOOL_PATH,
                              cg.params.chunk)
    t.assert_equals(os.execute(cmd), 0)
end

-- When running Lua code from script file.
g.test_debug_module_via_script = function(cg)
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'test_debug_module_via_script.lua'
    treegen.write_file(dir, script_name, cg.params.chunk)
    local opts = { nojson = true }
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.exit_code, 0)
end
