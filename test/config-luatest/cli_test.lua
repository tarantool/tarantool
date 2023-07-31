local t = require('luatest')
local justrun = require('test.justrun')

local g = t.group()

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
            default = '{{ instance_name }}.control',
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
