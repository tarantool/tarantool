local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

--
-- The test covers the case when synchronous transactions got CONFIRM entry
-- written with a smaller quorum than was set after a restart.
--
-- Then the transactions having seemingly not enough ACKs are getting CONFIRMs
-- right during recovery, which could, before a fix, break an invariant in the
-- synchronous queue implementation. Specifically, the in-memory volatile
-- confirmed LSN was lagging behind the actually persisted confirmed LSN, and
-- there was an assertion failure because of that.
--

g.before_each(function(cg)
    cg.master = server:new({
        box_cfg = {
            election_mode = 'manual',
        }
    })
    cg.master:start()
end)

g.after_each(function(cg)
    cg.master:drop()
end)

g.test_case = function(cg)
    cg.master:exec(function()
        -- Force the quorum 1 to keep confirming txns without having to
        -- start a second instance.
        box.cfg{replication_synchro_quorum = 1}
        local s = box.schema.create_space('test', {is_sync = true})
        s:create_index('pk')
        s:replace{1}
        -- Simulate like a new replica has joined and acked the
        -- next txn.
        box.space._cluster:replace{2, require('uuid').str()}
        s:replace{2}
    end)
    --
    -- On restart the synchro queue will replay CONFIRM for {1}. Then will see a
    -- quorum increase due to a new entry in _cluster. And then will replay the
    -- CONFIRM for {2}, without the ack from a second instance.
    --
    cg.master:restart()
    cg.master:exec(function()
        t.assert_equals(box.info.synchro.quorum, 2)
        t.assert_equals(box.space.test:select{}, {{1}, {2}})
    end)
end
