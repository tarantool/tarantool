local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local schema = require('experimental.config.utils.schema')

local g = t.group()

g.test_human_readable_value = function()
    local config = cbuilder:new()
        :set_global_option('lua.memory', '256 MiB')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    cluster['i-001']:exec(function()
        local config = require('config')

        t.assert_equals(config:get('lua.memory'), 256 * 1024 * 1024)
    end)
end

g.test_fromenv = function()
    local s = schema.scalar({
        type = 'integer',
        byte_size = true,
    })

    t.assert_equals(schema.fromenv('TT_READAHEAD', '32 KiB', s), 32 * 1024)
end
