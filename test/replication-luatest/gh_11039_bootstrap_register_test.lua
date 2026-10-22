local t = require('luatest')
local replica_set = require('luatest.replica_set')
local fio = require('fio')

local g = t.group()
local wait_timeout = 120

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        box_cfg = {
            election_mode = 'manual',
            replication_synchro_quorum = 'N',
            replication_synchro_timeout = wait_timeout,
        },
    })
    cg.master:start()
    cg.master:exec(function()
        box.ctl.promote()
        local s = box.schema.space.create('test', {is_sync = true})
        s:create_index('pk')
        s:insert{1}
    end)
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {cg.master.net_box_uri},
            instance_name = 'replica',
            read_only = true,
        },
    })
end)

g.after_each(function(cg)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_REGISTER_DELAY', false)
        box.cfg{replication_synchro_quorum = 1}
    end)
    cg.replica_set:drop()
end)

local function assert_registered(cg)
    cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    cg.replica:exec(function()
        t.assert_not_equals(box.info.id, 0)
        t.assert_equals(box.info.name, 'replica')
        t.assert_equals(box.space.test:select(), {{1}})
    end)
    cg.master:exec(function()
        t.assert_equals(box.space._cluster:count(), 2)
    end)
end

g.test_bootstrap = function(cg)
    cg.replica:start()
    assert_registered(cg)
    cg.replica:restart()
    assert_registered(cg)
end

g.test_anonymous_bootstrap = function(cg)
    cg.replica.box_cfg.instance_name = nil
    cg.replica.box_cfg.replication_anon = true
    cg.replica:start()
    cg.replica:assert_follows_upstream(cg.master:get_instance_id())
    cg.replica:exec(function()
        t.assert_equals(box.info.id, 0)
        t.assert_equals(box.space.test:select(), {{1}})
    end)
    cg.master:exec(function()
        t.assert_equals(box.space._cluster:count(), 1)
    end)
end

g.test_sync_transaction_during_register = function(cg)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_REGISTER_DELAY', true)
    end)
    cg.replica:start({wait_until_ready = false})
    cg.master:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.space._cluster:count(), 2)
            t.assert_equals(box.info.synchro.quorum, 2)
        end)
        local lsn = box.info.lsn
        local fiber = require('fiber')
        local f = fiber.new(function() box.space.test:insert{2} end)
        f:set_joinable(true)
        rawset(_G, 'insert_f', f)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.synchro.queue.len, 1)
            t.assert_gt(box.info.lsn, lsn)
        end)
        -- Include the unconfirmed transaction in the registration stream.
        box.error.injection.set('ERRINJ_REPLICA_REGISTER_DELAY', false)
    end, {wait_timeout})
    cg.replica:wait_until_ready()
    cg.master:exec(function(timeout)
        t.assert((_G.insert_f:join(timeout)))
        _G.insert_f = nil
        t.assert_equals(box.info.synchro.queue.len, 0)
    end, {wait_timeout})
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        t.assert_equals(box.space.test:select(), {{1}, {2}})
        t.assert_equals(box.info.synchro.queue.len, 0)
    end)
end

g.test_restart_during_registration = function(cg)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_REGISTER_DELAY', true)
    end)
    cg.replica:start({wait_until_ready = false})
    cg.master:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.space._cluster:count(), 2)
        end)
    end, {wait_timeout})
    -- The first checkpoint has no own ID or name yet.
    local snapshots = fio.glob(fio.pathjoin(cg.replica.workdir, '*.snap'))
    t.assert_not_equals(snapshots, {})
    cg.replica.process:kill(9)
    t.helpers.retrying({timeout = wait_timeout}, function()
        t.assert_not(cg.replica.process:is_alive())
    end)
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_REPLICA_REGISTER_DELAY', false)
    end)
    cg.replica:restart()
    assert_registered(cg)
end
