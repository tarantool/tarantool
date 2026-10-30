local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

-- Verify that if the iproto.listen option is removed from the
-- declarative configuration, it is removed from the box-level
-- configuration (box.cfg.listen) after config:reload().
g.test_basic = function()
    local uri = 'unix/:./var/run/i-001.iproto'

    local config = cbuilder:new()
        -- Discard cbuilder's default iproto.listen value and set
        -- it for the instance instead.
        :set_global_option('iproto.listen', nil)
        :add_instance('i-001', {
            iproto = {
                listen = {{uri = uri}},
            },
        })
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    -- Verify a test case prerequisite: the option is applied.
    cluster['i-001']:exec(function(uri)
        t.assert_equals(box.cfg.listen, {{uri = uri}})
    end, {uri})

    -- Remove the iproto.listen option, write and reload the new
    -- configuration.
    local config_2 = cbuilder:new(config)
        :set_instance_option('i-001', 'iproto.listen', nil)
        :config()
    cluster:reload(config_2)

    -- Verify that the option is set to its default.
    cluster['i-001']:exec(function()
        t.assert_equals(box.cfg.listen, nil)
        t.assert_type(box.cfg.listen, 'nil')
    end)
end
