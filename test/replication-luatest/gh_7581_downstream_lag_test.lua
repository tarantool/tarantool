local fiber = require("fiber")
local t = require("luatest")
local server = require("luatest.server")
local cluster = require("luatest.replica_set")

local g = t.group("downstream-lag-after-master-switch")

local POLL_TIMEOUT = 0.1
local DISCONNECT_TIMEOUT = 4 * POLL_TIMEOUT
--
-- Downstream lag calculations were off after master switch or after a period of
-- time with no transactions: downstream lag showed time since the last
-- processed transaction.
--
g.before_each(function(g)
    g.cluster = cluster:new{}
    local box_cfg = {
        replication_timeout = POLL_TIMEOUT,
        replication = {
            server.build_listen_uri("server1", g.cluster.id),
            server.build_listen_uri("server2", g.cluster.id),
        },
    }
    g.server1 = g.cluster:build_and_add_server({
        alias = "server1",
        box_cfg = box_cfg,
    })
    g.server2 = g.cluster:build_and_add_server({
        alias = "server2",
        box_cfg = box_cfg,
    })
    g.cluster:start()
    g.cluster:wait_for_fullmesh()
    g.server1:exec(function()
        box.schema.space.create("test")
        box.space.test:create_index("pk")
    end)
end)

g.after_each(function(g)
    g.cluster:drop()
end)

local function get_downstream_lag(master, replica)
    local id = replica:get_instance_id()
    return master:exec(function(id)
        return require("luatest").helpers.retrying({}, function()
            return box.info.replication[id].downstream.lag
        end)
    end, {id})
end

local function wait_downstream_updated(master, replica)
    local id = replica:get_instance_id()
    t.helpers.retrying({}, function()
        master:exec(function(id)
            -- Ignore local lsn
            local vclock = table.deepcopy(box.info.vclock)
            vclock[0] = nil
            t.assert_equals(vclock,
                            box.info.replication[id].downstream.vclock,
                            "Downstream vclock is updated")
        end, {id})
    end)
end

g.test_downstream_lag = function(g)
    g.server1:exec(function()
        box.space.test:insert{1}
    end)
    g.server2:wait_for_vclock_of(g.server1)
    wait_downstream_updated(g.server1, g.server2)
    local lag = get_downstream_lag(g.server1, g.server2)
    t.assert(lag ~= 0, "Real lag value is updated")
    t.assert(lag < DISCONNECT_TIMEOUT, "Lag value is sane")
    -- Let a couple of pings pass through.
    fiber.sleep(2 * POLL_TIMEOUT);
    lag = get_downstream_lag(g.server1, g.server2)
    t.assert_equals(lag, get_downstream_lag(g.server1, g.server2),
                    "Lag doesn't change on standby")
    g.server2:exec(function()
        box.space.test:insert{2}
    end)
    -- Wait for relay -> tx status update on server 1. It shouldn"t spoil the
    -- downstream lag.
    g.server1:wait_for_vclock_of(g.server2)
    wait_downstream_updated(g.server1, g.server2)
    t.assert_equals(lag, get_downstream_lag(g.server1, g.server2),
                    "Lag doesn't change when there are updates from remote \
                     servers")
end
