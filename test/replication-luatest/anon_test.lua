local server = require('luatest.server')
local t = require('luatest')
local g = t.group()

local wait_timeout = 60

g.before_all = function(lg)
    lg.master = server:new({alias = 'master'})
    lg.master:start()
end

g.after_all = function(lg)
    lg.master:drop()
end

--
-- gh-9916: txns being applied from the master during the name change process
-- could crash the replica or cause "double LSN" error in release.
--
g.test_txns_replication_during_registration = function(lg)
    t.tarantool.skip_if_not_debug()
    lg.master:exec(function()
        local s = box.schema.create_space('test')
        s:create_index('pk')
    end)
    local box_cfg = {
        replication_anon = true,
        read_only = true,
        replication = {
            lg.master.net_box_uri,
        },
    }
    local replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    replica:start()
    replica:exec(function()
        t.assert_equals(box.info.id, 0)
        box.error.injection.set("ERRINJ_WAL_DELAY_COUNTDOWN", 0)
    end)
    lg.master:exec(function()
        box.space.test:replace{1}
    end)
    replica:exec(function(timeout)
        -- One txn from master is being applied by the replica. Not yet
        -- reflected in its vclock.
        t.helpers.retrying({timeout = timeout}, function()
            if box.error.injection.get("ERRINJ_WAL_DELAY") then
                return
            end
            error("No txn from master")
        end)
        local fiber = require('fiber')
        -- Send registration request while having a remote txn being committed
        -- from the same master.
        local f = fiber.create(function() box.cfg{replication_anon = false} end)
        f:set_joinable(true)
        rawset(_G, 'test_f', f)
    end, {wait_timeout})
    lg.master:exec(function(timeout)
        t.helpers.retrying({timeout = timeout}, function()
            if box.space._cluster:count() == 2 then
                return
            end
            error("No registration request from replica")
        end)
        -- Bump the LSN again. To make it easier later to wait when the replica
        -- gets all previous data from the master.
        box.space.test:replace{2}
    end, {wait_timeout})
    local replica_id = replica:exec(function()
        box.error.injection.set("ERRINJ_WAL_DELAY", false)
        local ok, err = _G.test_f:join()
        _G.test_f = nil
        t.assert_equals(err, nil)
        t.assert(ok)
        local id = box.info.id
        t.assert_not_equals(id, 0)
        return id
    end)
    replica:wait_for_vclock_of(lg.master)
    replica:exec(function()
        t.assert(box.space.test:get{2}, {2})
    end)

    -- Cleanup.
    replica:drop()
    lg.master:exec(function(id)
        box.space.test:drop()
        box.space._cluster:delete{id}
    end, {replica_id})
end
