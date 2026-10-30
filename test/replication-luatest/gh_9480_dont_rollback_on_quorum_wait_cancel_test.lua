local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_each(function(cg)
    local box_cfg = {
        election_mode='off',
        replication_synchro_timeout=1000,
        replication_synchro_quorum = 2,
        memtx_use_mvcc_engine = false,
    }
    cg.server = server:new({box_cfg = box_cfg})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- Test that if we cancel TX while it is waiting quorum in limbo it is
-- not rolled back.
g.test_cancel_tx_waiting_in_limbo = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')

        local space = box.schema.create_space('test', {is_sync = true})
        space:create_index('pk')
        box.ctl.promote()

        local f = fiber.new(function()
            space:insert({1})
        end)
        f:set_joinable(true)
        f:wakeup()

        t.helpers.retrying({timeout = 3}, function()
            t.assert(box.info.synchro.queue.len == 1)
        end)
        f:cancel()
        local ret, err = f:join()
        t.assert_equals(ret, false)
        t.assert_equals(err:unpack().type, 'FiberIsCancelled')
        t.assert_not_equals(space:get({1}), nil)
    end)
end
