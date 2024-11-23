local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.s = server:new({
        alias = 'default',
        box_cfg = {
            replication_synchro_timeout = 120,
            replication_synchro_quorum = 2,
        },
    })
    cg.s:start()
    cg.s:exec(function()
        box.schema.create_space('a', {is_sync = false}):create_index('p')
        box.schema.create_space('s', {is_sync = true}):create_index('p')
        box.ctl.promote()
    end)
end)

g.after_each(function(cg)
    cg.s:drop()
end)

-- Test that asynchronously committing an async transaction after a sync
-- transaction that later times out on `replication_synchro_timeout` works
-- correctly.
g.test_async_commit_async_async_tx_after_sync_tx_synchro_timeout = function(cg)
   cg.s:exec(function()
       local f = require('fiber').create(function() box.space.s:replace{0} end)
       f:set_joinable(true)
       box.atomic({wait = 'none'}, function() box.space.a:replace{0} end)
       t.assert_equals(box.info.synchro.queue.len, 2)
       box.cfg{replication_synchro_timeout = 0.001}
       t.assert_not(f:join())
       t.assert_equals(box.info.synchro.queue.len, 0)
       t.assert_equals(box.space.a:get{0}, nil)
   end)
end
