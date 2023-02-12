local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

-- Core idea: return something that differs from the corresponding
-- built-in module.
--
-- Print `...` and `arg` to ensure that they have expected
-- value.
local OVERRIDE_SCRIPT_TEMPLATE = [[
print(require('json').encode({
    ['script'] = '<script>',
    ['...'] = {...},
    ['arg[-1]'] = arg[-1],
    ['arg[0]'] = arg[0],
    ['arg[]'] = setmetatable(arg, {__serialize = 'seq'}),
}))
return {whoami = 'override.<module_name>'}
]]

-- Print a result of the require call.
local MAIN_SCRIPT_TEMPLATE = [[
print(require('json').encode({
    ['script'] = '<script>',
    ['<module_name>'] = require('<module_name>'),
}))
]]

g.before_all(function(g)
    treegen.init(g)
    treegen.add_template(g, '^override/.*%.lua$', OVERRIDE_SCRIPT_TEMPLATE)
    treegen.add_template(g, '^main%.lua$', MAIN_SCRIPT_TEMPLATE)
end)

g.after_all(function(g)
    treegen.clean(g)
end)

-- Test oracle.
--
-- Verifies that the override module is actually returned by the
-- require call in main.lua.
--
-- Also holds `...` (without 'override.') and `arg` (the same as
-- in the main script).
local function expected_output(module_name)
    local module_name_as_path = table.concat(module_name:split('.'), '/')
    local override_filename = ('override/%s.lua'):format(module_name_as_path)

    local res = {
        {
            ['script'] = override_filename,
            ['...'] = {module_name},
            ['arg[-1]'] = arg[-1],
            ['arg[0]'] = 'main.lua',
            ['arg[]'] = {},
        },
        {
            ['script'] = 'main.lua',
            [module_name] = {
                whoami = ('override.%s'):format(module_name)
            },
        },
    }

    return {
        exit_code = 0,
        stdout = res,
    }
end

-- A couple of test cases with overriding built-in modules.
--
-- In a general case it is not a trivial task to correctly
-- override a built-in module. However, there are modules that
-- could be overridden with an arbitrary table and it will pass
-- tarantool's initialization successfully.
--
-- We have no guarantee that any module could be overridden. It is
-- more like 'it is generally possible'.
--
-- The list is collected from loaders.builtin and package.loaded
-- keys. Many modules are excluded deliberately:
--
-- - json -- it is used in the test itself
-- - bit, coroutine, debug, ffi, io, jit, jit.*, math, os,
--   package, string, table -- LuaJIT modules
-- - misc -- tarantool's module implemented as part of LuaJIT.
-- - *.lib, internal.*, utils.* and so on -- tarantool internal
--   modules
-- - memprof.*, misc.*, sysprof.*, table.*, timezones -- unclear
--   whether they're public
-- - box, buffer, decimal, errno, fiber, fio, log, merger,
--   msgpackffi, strict, tarantool, yaml -- used during
--   tarantool's initialization in a way that doesn't allow to
--   replace them with an arbitrary table
local override_cases = {
    'clock',
    'console',
    'crypto',
    'csv',
    'datetime',
    'digest',
    'error',
    'fun',
    'help',
    'http.client',
    'iconv',
    'key_def',
    'luadebug',
    'memprof',
    'msgpack',
    'net.box',
    'pickle',
    'popen',
    'pwd',
    'socket',
    'swim',
    'sysprof',
    'tap',
    'title',
    'uri',
    'utf8',
    'uuid',
    'xlog',
}

-- Generate a workdir (override/foo.lua and main.lua), run
-- tarantool, check output.
for _, module_name in ipairs(override_cases) do
    local module_name_as_path = table.concat(module_name:split('.'), '/')
    local override_filename = ('override/%s.lua'):format(module_name_as_path)
    local module_name_as_snake = table.concat(module_name:split('.'), '_')
    local case_name = ('test_override_%s'):format(module_name_as_snake)

    g[case_name] = function(g)
        local scripts = {override_filename, 'main.lua'}
        local replacements = {module_name = module_name}
        local dir = treegen.prepare_directory(g, scripts, replacements)
        local res = justrun.tarantool(dir, {}, {'main.lua'})
        local exp = expected_output(module_name)
        t.assert_equals(res, exp)
    end
end
