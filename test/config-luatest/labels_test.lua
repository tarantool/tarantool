local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

-- Verify that instance labels are merged correctly.
g.test_labels = function()
    local config = cbuilder:new()
        :set_replicaset_option('labels', {
            foo = 'true',
            bar = 'true',
        })
        :add_instance('instance-001', {
            labels = {
                baz = 'true',
                foo = 'false',
            },
        })
        :config()

    local cluster = cluster:new(config)
    cluster:start()

    cluster['instance-001']:exec(function()
        local config = require('config')

        t.assert_equals(config:get('labels'), {
            foo = 'false',
            bar = 'true',
            baz = 'true',
        })
    end)
end
