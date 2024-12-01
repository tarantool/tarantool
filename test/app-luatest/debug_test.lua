local t = require('luatest')

local justrun = require('luatest.justrun')
local server = require('luatest.server')
local treegen = require('luatest.treegen')

local SCRIPT_NAME = 'test_debug_module_via_script.lua'

local tests = {
    {
        chunk = [[print(debug.sourcefile())]],
        res_console = 'nil',
        res_script = SCRIPT_NAME,
    },
    {
        chunk = [[print(debug.sourcedir())]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk = [[print(debug.__file__)]],
        res_console = 'nil',
        res_script = SCRIPT_NAME,
    },
    {
        chunk = [[print(debug.__dir__)]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk = [[print(require('net.box').self:call('debug.sourcefile'))]],
        res_console = '',
        res_script = '',
    },
    {
        chunk = [[print(require('net.box').self:call('debug.sourcedir'))]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk = [[fn = function() return debug.__file__ end;
                  print(require('net.box').self:call('fn'))]],
        res_console = '',
        res_script = SCRIPT_NAME,
    },
    {
        chunk = [[fn = function() return debug.__dir__ end;
                  print(require('net.box').self:call('fn'))]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk = [[fn = function() local res = debug.sourcefile();
                  return res end;
                  print(require('net.box').self:call('fn'))]],
        res_console = '',
        res_script = SCRIPT_NAME,
    },
    {
        chunk = [[fn = function() local res = debug.sourcedir(); return res end;
                  print(require('net.box').self:call('fn'))]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk = [[print(loadstring('return debug.sourcefile()')())]],
        res_console = 'nil',
        res_script = SCRIPT_NAME,
    },
    {
        chunk = [[print(loadstring('return debug.sourcedir()')())]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk = [[print(loadstring('return debug.__file__')())]],
        res_console = 'nil',
        res_script = 'nil',
    },
    {
        chunk = [[print(loadstring('return debug.__dir__')())]],
        res_console = '.',
        res_script = '.',
    },
    {
        chunk =
        [[print(loadstring('local res = debug.sourcefile(); return res')())]],
        res_console = 'nil',
        res_script = 'nil',
    },
    {
        chunk =
        [[print(loadstring('local res = debug.sourcedir(); return res')())]],
        res_console = '.',
        res_script = '.',
    },
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
    local NULL = box.NULL
    local conn = g_remote.server.net_box

    local result, err = conn:call('debug.sourcefile')
    t.assert_equals(result, NULL, 'debug.sourcefile() returns NULL')
    t.assert_equals(err, nil, 'debug.sourcefile() returns no error')

    local result, err = conn:call('debug.sourcedir')
    t.assert_equals(result, '.', 'debug.sourcedir() returns "."')
    t.assert_equals(err, nil, 'debug.sourcedir() returns no error')
end

local g_console = t.group('console', tests)

-- When running Lua code from the console.
-- Note, `debug.sourcefile()` returns a current working
-- directory when running within console.
g_console.test_debug_module_via_console = function(cg)
    local dir = treegen.prepare_directory({}, {})
    local opts = { nojson = true, quote_args = true }
    local res = justrun.tarantool(dir, {}, { '-e', cg.params.chunk }, opts)
    t.assert_equals(res.exit_code, 0, 'check command status')
    t.assert_equals(res.stdout, cg.params.res_console, 'check command stdout')
end

local g_script = t.group('script', tests)

-- When running Lua code from a script file.
g_script.test_debug_module_via_script = function(cg)
    local dir = treegen.prepare_directory({}, {})
    local script_name = SCRIPT_NAME
    treegen.write_file(dir, script_name, cg.params.chunk)
    local opts = { nojson = true }
    local res = justrun.tarantool(dir, {}, { script_name }, opts)
    t.assert_equals(res.stdout, cg.params.res_script, 'check command stdout')
    t.assert_equals(res.exit_code, 0, 'check command status')
end
