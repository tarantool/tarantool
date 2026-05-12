local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local schema = require('experimental.config.utils.schema')

local g = t.group()

g.test_human_readable_timeout = function()
    local config = cbuilder:new()
        :set_global_option('replication.timeout', '2m')
        :set_global_option('replication.election_timeout', '300ms')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    cluster['i-001']:exec(function()
        local config = require('config')

        t.assert_equals(config:get('replication.timeout'), 2 * 60)
        t.assert_equals(config:get('replication.election_timeout'), 0.3)
        t.assert_equals(box.cfg.replication_timeout, 2 * 60)
        t.assert_equals(box.cfg.election_timeout, 0.3)
    end)
end

g.test_human_readable_fiber_durations = function()
    local config = cbuilder:new()
        :set_global_option('fiber.too_long_threshold', '1.5s')
        :set_global_option('fiber.slice.warn', '2s')
        :set_global_option('fiber.slice.err', '3s')
        :add_instance('i-001', {})
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    cluster['i-001']:exec(function()
        local config = require('config')

        t.assert_equals(config:get('fiber.too_long_threshold'), 1.5)
        t.assert_equals(config:get('fiber.slice.warn'), 2)
        t.assert_equals(config:get('fiber.slice.err'), 3)
    end)
end

g.test_fromenv = function()
    local s = schema.scalar({
        type = 'number',
        duration = true,
    })

    t.assert_equals(schema.fromenv('TT_REPLICATION_TIMEOUT', '2m', s), 2 * 60)
end
