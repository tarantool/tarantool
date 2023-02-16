local fio = require('fio')

local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

local MODULE_SCRIPT_TEMPLATE = [[
print(require('json').encode({
    ['script'] = '<script>',
}))
return {whoami = '<module_name>'}
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
    treegen.add_template(g, '^main%.lua$', MAIN_SCRIPT_TEMPLATE)
    treegen.add_template(g, '^.*%.lua$', MODULE_SCRIPT_TEMPLATE)
end)

g.after_all(function(g)
    treegen.clean(g)
end)

local function expected_output(module_relpath, module_name)
    local res = {
        {
            ['script'] = module_relpath,
        },
        {
            ['script'] = 'main.lua',
            [module_name] = {
                whoami = module_name,
            },
        },
    }

    return {
        exit_code = 0,
        stdout = res,
    }
end

-- Create an 'application directory' with a main script and a
-- module. Run the main script and ensure that the module is
-- successfully required.
for _, case in ipairs({
    -- Application's 'foo' module.
    {'foo.lua', 'foo'},
    {'foo/init.lua', 'foo'},
    {'app/foo.lua', 'app.foo'},
    {'app/foo/init.lua', 'app.foo'},
    {'.rocks/share/tarantool/foo.lua', 'foo'},
    {'.rocks/share/tarantool/foo/init.lua', 'foo'},
    {'.rocks/share/tarantool/app/foo.lua', 'app.foo'},
    {'.rocks/share/tarantool/app/foo/init.lua', 'app.foo'},
    -- Application's socket override module.
    --
    -- See also override_test.lua.
    {'override/socket.lua', 'socket'},
    {'.rocks/share/tarantool/override/socket.lua', 'socket'},
}) do
    local module_relpath = case[1]
    local module_name = case[2]

    local module_slug = module_relpath
        :gsub('^%.rocks/share/tarantool/', 'rocks/'):gsub('/', '_'):sub(1, -5)

    g['test_' .. module_slug] = function(g)
        local scripts = {module_relpath, 'main.lua'}
        local replacements = {module_name = module_name}
        local dir = treegen.prepare_directory(g, scripts, replacements)
        local main_script = fio.pathjoin(dir, 'main.lua')
        -- The current working directory is in the filesystem
        -- root, so the only way to reach the modules is to search
        -- for them next to the script.
        --
        -- If we would run tarantool from inside the application
        -- directory, the module would be found just because it is
        -- in the current directory.
        local res = justrun.tarantool('/', {}, {main_script})
        local exp = expected_output(module_relpath, module_name)
        t.assert_equals(res, exp)
    end
end
