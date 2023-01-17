local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {
            memtx_use_mvcc_engine = true,
            replication_synchro_quorum = 2,
            replication_synchro_timeout = 0.0001,
        }
    }
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create("s", {is_sync = true})
        s:create_index("pk")
        local as = box.schema.space.create("as")
        as:create_index("pk")

        box.ctl.promote()
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that preparation of an insert statement with an older story deleted by
-- a prepared transaction does not fail assertion.
g.test_preparation_with_deleted_older_story_assertion = function(cg)
    cg.server:exec(function()
        local t = require('luatest')

        box.space.as:replace{0}

        t.assert_error_msg_content_equals(
                'Quorum collection for a synchronous transaction is timed out',
                function()
                    box.atomic(function()
                        box.space.s:replace{0}
                        box.space.as:delete{0}
                    end)
                end)
        t.assert_equals(box.space.as:get{0}, {0})
    end)
end
