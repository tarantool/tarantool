local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

local OVERRIDE_SCRIPT_TEMPLATE = [[
local loaders = require('internal.loaders')

return {
    whoami = 'override.<module_name>',
    initializing = loaders.initializing,
}
]]

-- Print a result of the require call.
local MAIN_SCRIPT_TEMPLATE = [[
local json = require('json')
local loaders = require('internal.loaders')

print(json.encode({
    ['<module_name>'] = require('<module_name>'),
    ['<script>'] = {
        whoami = '<script>',
        initializing = loaders.initializing,
    }
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

local function expected_output(module_name)
    local res = {
        {
            [module_name] = {
                whoami = ('override.%s'):format(module_name),
                initializing = true,
            },
            ['main.lua'] = {
                whoami = 'main.lua',
                -- initializing is nil
            }
        }
    }

    return {
        exit_code = 0,
        stdout = res,
    }
end

g.test_initializing = function(g)
    local scripts = {'override/socket.lua', 'main.lua'}
    local replacements = {module_name = 'socket'}
    local dir = treegen.prepare_directory(g, scripts, replacements)
    local res = justrun.tarantool(dir, {}, {'main.lua'})
    local exp = expected_output('socket')
    t.assert_equals(res, exp)
end

g.test_no_package_loaded = function()
    local LOAD_LIMIT = 3
    local LOAD_ATTEMPTS = LOAD_LIMIT + 1

    local load_cnt = 0
    local foo = {whoami = 'foo'}
    package.preload.foo = function()
        local loaders = require('internal.loaders')
        load_cnt = load_cnt + 1
        if load_cnt < LOAD_LIMIT then
            loaders.no_package_loaded.foo = true
        end
        return foo
    end

    for _ = 1, LOAD_ATTEMPTS do
        require('foo')
    end

    package.preload.foo = nil
    t.assert_equals(load_cnt, LOAD_LIMIT)
end
