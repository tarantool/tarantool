local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local g = t.group()

local wait_timeout = 120

local function wait_for_registration(master, wait_timeout)
    master:exec(function(wait_timeout)
        t.helpers.retrying({timeout = wait_timeout}, function()
            t.assert_equals(box.space._cluster:count(), 2)
        end)
    end, {wait_timeout})
end

local function assert_replica_follows_the_master(replica, master_id)
    replica:exec(function(id, timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_equals(box.info.replication[id].upstream.status, 'follow')
        end)
    end, {master_id, wait_timeout})
end

g.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server({alias = 'master'})
    cg.master_uri = server.build_listen_uri('master', cg.replica_set.id)
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        box_cfg = {
            replication = {cg.master_uri},
            replication_anon = true,
            read_only = true,
        },
    })
    cg.replica_set:start()
end)

g.after_each(function(cg)
    cg.replica_set:drop()
end)

local function crash_during_registration(cg)
    local master_id = cg.master:get_instance_id()
    assert_replica_follows_the_master(cg.replica, master_id)
    cg.replica:exec(function()
        -- Keep the own _cluster row out of the WAL when it arrives.
        box.error.injection.set('ERRINJ_WAL_DELAY', true)
    end)
    cg.replica:exec(function()
        local fiber = require('fiber')
        local f = fiber.create(function()
            box.cfg{replication_anon = false}
        end)
        f:set_joinable(true)
        rawset(_G, 'switch_to_named_f', f)
    end)
    -- The master has already committed the registration
    wait_for_registration(cg.master, wait_timeout)
    cg.replica:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            t.assert_not_equals(box.info.id, 0)
        end)
        t.assert(box.error.injection.get('ERRINJ_WAL_DELAY'))
    end, {wait_timeout})
    -- Crash the replica right in the middle of the registration.
    cg.replica.process:kill(9)
    t.helpers.retrying({timeout = wait_timeout}, function()
        t.assert_not(cg.replica.process:is_alive())
    end)
    return master_id
end

g.test_anon_replica_crash_during_register = function(cg)
    local master_id = crash_during_registration(cg)
    cg.replica:restart({box_cfg = {
        replication = {cg.master_uri},
        replication_anon = false,
        election_mode = 'voter',
        instance_name = 'replica',
    }})
    -- The replica started fine and re-registered on the master, which
    -- already has its row committed.
    assert_replica_follows_the_master(cg.replica, master_id)
    wait_for_registration(cg.master, wait_timeout)
    cg.replica:exec(function()
        t.assert_equals(box.info.name, 'replica')
    end)
end

g.test_registration_error_on_restart = function(cg)
    crash_during_registration(cg)
    cg.master:exec(function()
        -- Fail after sending the own _cluster row, before REGISTER completes.
        box.error.injection.set('ERRINJ_RELAY_FINAL_JOIN', true)
    end)
    cg.replica:restart({box_cfg = {
        replication = {cg.master_uri},
        replication_anon = false,
    }}, {wait_until_ready = false})
    t.helpers.retrying({timeout = wait_timeout}, function()
        t.assert_not(cg.replica.process:is_alive())
    end)
    t.assert(cg.replica:grep_log("can't initialize storage: " ..
                               "Error injection 'relay final join'"))
end
