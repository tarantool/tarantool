local replica_set = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    cg.box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.replica_set.id),
            server.build_listen_uri('replica', cg.replica_set.id),
        },
        replication_timeout = 0.1,
        replication_synchro_timeout = 120,
    }
    cg.master = cg.replica_set:build_and_add_server{
        alias = 'master',
        box_cfg = cg.box_cfg,
    }
    cg.replica = cg.replica_set:build_and_add_server{
        alias = 'replica',
        box_cfg = cg.box_cfg,
    }
    cg.replica_set:start()
    cg.replica_set:wait_for_fullmesh()
    cg.master:exec(function()
        rawset(_G, 'fiber', require('fiber'))

        box.schema.space.create('s', {is_sync = true}):create_index('p')
        box.ctl.promote()
    end)
    cg.master:wait_for_downstream_to(cg.replica)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

-- Test that the CONFIRM request WAL write is not done if the limbo is
-- `in_rollback` state.
g.test_limbo_in_rollback_confirm_not_written = function(cg)
    t.tarantool.skip_if_not_debug()

    local fid = cg.master:exec(function()
        -- Delay the limbo worker wake up.
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', true)
        -- Make the PROMOTE request WAL write hang.
        box.error.injection.set('ERRINJ_WAL_DELAY_COUNTDOWN', 1)
        local f = _G.fiber.create(function() box.space.s:replace{0} end)
        f:set_joinable(true)
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
        return f:id()
    end)

    cg.master:wait_for_downstream_to(cg.replica)

    cg.replica:exec(function()
        t.helpers.retrying({timeout = 120}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
        end)
        box.cfg{replication_synchro_timeout = 0.01}
        box.ctl.promote()
    end)

    cg.master:exec(function(fid)
        t.helpers.retrying({timeout = 120}, function()
            t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
        end)
        t.assert(box.info.synchro.queue.busy)

        local write_count = box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
        box.error.injection.set('ERRINJ_TXN_LIMBO_WORKER_DELAY', false)
        _G.fiber.sleep(0.1)
        t.assert_equals(box.info.synchro.queue.len, 1)
        box.error.injection.set('ERRINJ_WAL_DELAY', false)
        t.assert(_G.fiber.find(fid):join())
        -- Check that CONFIRM request was not written to the WAL.
        t.assert_equals(
            box.error.injection.get('ERRINJ_WAL_WRITE_COUNT'), write_count)
    end, {fid})
end
