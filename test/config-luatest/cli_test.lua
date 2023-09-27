local t = require('luatest')
local justrun = require('test.justrun')
local treegen = require('test.treegen')

local g = t.group()

g.before_all(function()
    treegen.init(g)
end)

g.after_all(function()
    treegen.clean(g)
end)

g.test_help_env_list = function()
    local res = justrun.tarantool('.', {}, {'--help-env-list'}, {nojson = true})
    t.assert_equals(res.exit_code, 0)

    -- Pick up several well-known env variables to verify the idea
    -- of the listing, but at the same time, don't list all the
    -- options in the test case.
    local cases = {
        -- Verify that the env variables that duplicates CLI
        -- options are present.
        {
            name = 'TT_INSTANCE_NAME',
            type = 'string',
            default = 'N/A',
            availability = 'Community Edition',
        },
        {
            name = 'TT_CONFIG',
            type = 'string',
            default = 'nil',
            availability = 'Community Edition',
        },
        -- An env variable that accepts a numeric value and has
        -- explicit default value.
        {
            name = 'TT_MEMTX_MEMORY',
            type = 'integer',
            default = '268435456',
            availability = 'Community Edition',
        },
        -- An env variable that has box.NULL default.
        {
            name = 'TT_DATABASE_MODE',
            type = 'string',
            default = 'box.NULL',
            availability = 'Community Edition',
        },
        -- An option with a template default.
        {
            name = 'TT_CONSOLE_SOCKET',
            type = 'string',
            default = 'var/run/{{ instance_name }}/tarantool.control',
            availability = 'Community Edition',
        },
        -- An Enterprise Edition option and, at the same time,
        -- an option with non-string type.
        {
            name = 'TT_CONFIG_ETCD_ENDPOINTS',
            type = 'array',
            default = 'nil',
            availability = 'Enterprise Edition',
        },
        -- Yet another Enterprise Edition option.
        {
            name = 'TT_WAL_EXT_NEW',
            type = 'boolean',
            default = 'nil',
            availability = 'Enterprise Edition',
        },
    }

    for _, case in ipairs(cases) do
        local needle = ('%s.*%s.*%s.*%s'):format(case.name, case.type,
            case.default, case.availability)
        t.assert(res.stdout:find(needle), case.name)
    end
end

g.test_force_recovery = function()
    local dir = treegen.prepare_directory(g, {}, {})
    local script = [[
        box.cfg{}
        print(box.cfg.force_recovery)
        os.exit(0)
    ]]
    treegen.write_script(dir, 'main.lua', script)

    -- Make sure the CLI force-recovery option sets the box.cfg force_recovery
    -- option.
    local env = {}
    local opts = {nojson = true, stderr = false}
    local args = {'--force-recovery', 'main.lua'}
    local res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, 'true')

    -- Make sure the CLI force-recovery option sets the box.cfg force_recovery
    -- option to true even if the env TT_FORCE_RECOVERY option is set to false.
    env = {TT_FORCE_RECOVERY = false}
    opts = {nojson = true, stderr = false}
    args = {'--force-recovery', 'main.lua'}
    res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, 'true')

    -- Make sure the env option TT_FORCE_RECOVERY sets correctly if CLI
    -- force-recovery is not set.
    env = {TT_FORCE_RECOVERY = true}
    opts = {nojson = true, stderr = false}
    args = {'main.lua'}
    res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, 'true')

    -- Make sure the CLI force recovery option is overwritten by explicitly
    -- setting the force_recovery option in box.cfg.
    script = [[
        box.cfg{force_recovery = false}
        print(box.cfg.force_recovery)
        os.exit(0)
    ]]
    treegen.write_script(dir, 'two.lua', script)
    env = {}
    opts = {nojson = true, stderr = false}
    args = {'--force-recovery', 'two.lua'}
    res = justrun.tarantool(dir, env, args, opts)
    t.assert_equals(res.exit_code, 0)
    t.assert_equals(res.stdout, 'false')
end
