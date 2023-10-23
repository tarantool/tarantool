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

g.test_failover = function()
    t.tarantool.skip_if_enterprise()
    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool('.', {}, {'--failover'}, opts)
    t.assert_not_equals(res.exit_code, 0)
    t.assert_equals(res.stderr, '--failover CLI option is available only in ' ..
        'Tarantool Enterprise Edition')
end

-- Verify that valid TT_INSTANCE_NAME and --name values are
-- accepted.
g.test_name_success = function()
    local names = {
        'instance-001',
        'instance_001',
        ('x'):rep(63),
    }

    for _, name in ipairs(names) do
        -- If tarantool reports an empty configuration error, it
        -- means that --name/TT_INSTANCE_NAME validation passes.
        local exp_err = 'No cluster config received'

        -- Common options.
        local opts = {nojson = true, stderr = true}

        -- Run tarantool --name <...>.
        local args = {'--name', name}
        local res = justrun.tarantool('.', {}, args, opts)
        t.assert_not_equals(res.exit_code, 0)
        t.assert_str_contains(res.stderr, exp_err)

        -- Run TT_INSTANCE_NAME=<...> tarantool.
        local env = {['TT_INSTANCE_NAME'] = name}
        local res = justrun.tarantool('.', env, {}, opts)
        t.assert_not_equals(res.exit_code, 0)
        t.assert_str_contains(res.stderr, exp_err)
    end

end

-- Verify that invalid TT_INSTANCE_NAME and --name values are not
-- accepted.
g.test_name_failure = function()
    local err_must_start_from = 'A name must start from a lowercase letter, ' ..
        'got %q'
    local err_must_contain_only = 'A name must contain only lowercase ' ..
        'letters, digits, dash and underscore, got %q'

    local cases = {
        {
            name = '',
            exp_err = 'Zero length name is forbidden',
        },
        {
            name = ('x'):rep(64),
            exp_err = 'A name must fit 63 characters limit, got %q',
        },
        {
            name = '1st',
            exp_err = err_must_start_from,
        },
        {
            name = '_abc',
            exp_err = err_must_start_from,
        },
        {
            name = '_abC',
            exp_err = err_must_contain_only,
        },
        {
            name = 'Abc',
            exp_err = err_must_contain_only,
        },
        {
            name = 'abC',
            exp_err = err_must_contain_only,
        },
        {
            name = 'a.b',
            exp_err = err_must_contain_only,
        },
        {
            name = 'a b',
            exp_err = err_must_contain_only,
        },
    }

    for _, case in ipairs(cases) do
        -- Prepare expected error message.
        local exp_err = case.exp_err
        if exp_err:match('%%q') then
            exp_err = exp_err:format(case.name)
        end
        local exp_err = table.concat({
            ('LuajitError: [--name] %s'):format(exp_err),
            'fatal error, exiting the event loop',
        }, '\n')

        -- Common options.
        local opts = {nojson = true, stderr = true, quote_args = true}

        -- Run tarantool --name <...>.
        local args = {'--name', case.name}
        local res = justrun.tarantool('.', {}, args, opts)
        t.assert_not_equals(res.exit_code, 0)
        t.assert_equals(res.stderr, exp_err)

        -- Run TT_INSTANCE_NAME=<...> tarantool.
        local env = {['TT_INSTANCE_NAME'] = case.name}
        local res = justrun.tarantool('.', env, {}, opts)
        t.assert_not_equals(res.exit_code, 0)
        t.assert_equals(res.stderr, exp_err)
    end
end
