local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group()

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
        },
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = {
            replication = {
                server.build_listen_uri('master', cg.replica_set.id),
            },
            replication_timeout = 0.1,
            wal_queue_max_size = 1,
        },
    }
    cg.replica_set:start()
    cg.master:exec(function()
        box.schema.space.create('test_loc', {is_local = true})
        box.space.test_loc:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

--
-- gh-12719: applier fiber can get blocked while waiting for journal submit, if
-- the journal queue is full. While waiting, the applier can get cancelled (for
-- example, when its peer is removed from box.cfg.replication). This produced a
-- special error code for the cancelled transaction which wasn't handled on its
-- transformation into an error object, causing a panic-crash.
--
g.test_journal_queue_cancel_applier = function(cg)
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 0)
        local fiber = require('fiber')
        -- Block the WAL so any next txn will have to wait in the journal queue.
        rawset(_G, 'test_f', fiber.create(function()
            fiber.self():set_joinable(true)
            box.space.test_loc:replace{1}
        end))
        t.assert_equals(box.space.test_loc:select(), {{1}})
        t.helpers.retrying({timeout = 120}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
    end)
    cg.master:exec(function()
        -- This will get stuck in the applier on the replica.
        box.space._schema:insert{'test_key'}
    end)
    cg.replica:exec(function()
        -- The applier transaction is received and blocks the applier.
        t.helpers.retrying({timeout = 120}, function()
            t.assert_not_equals(box.space._schema:get('test_key'), nil)
        end)
        -- Cancel the applier.
        local old_replication = box.cfg.replication
        box.cfg{replication = {}}
        -- Transaction is rolled back.
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.space._schema:get('test_key'), nil)
        end)
        -- Retry.
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert((_G.test_f:join()))
        box.cfg{replication = old_replication}
        t.helpers.retrying({timeout = 120}, function()
            t.assert_not_equals(box.space._schema:get('test_key'), nil)
        end)
    end)
end
