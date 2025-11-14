local t = require("luatest")
local server = require("luatest.server")

local g = t.group()

g.before_all(function()
    g.reconnect_after = 0.01
    g.peer_closed_pattern = "%d+-%d+-%d+ %d+:%d+:%d+.%d+ .* " ..
                            "Peer closed"
    g.connection_refused_pattern = "%d+-%d+-%d+ %d+:%d+:%d+.%d+ .* " ..
                                   "Connection refused"
    g.router = server:new({alias = "router"})
    g.storage = server:new({alias = "storage"})
    g.router:start()
    g.storage:start()
end)

g.after_all(function()
    g.router:drop()
    g.storage:drop()
end)

g.before_each(function()
    -- Since connection doesn't die immediately after a test, it may happen,
    -- that previous tests affect the current one, which causes the flakiness.
    -- We need to restart our router in order to ensure that there are no
    -- connections.
    g.router:restart()
end)

--
-- This function is needed for checking that the socket we want to connect to
-- is in a certain state (bind (active) or closed). It can help us in scenario
-- in which the certain socket is binded by another process of external test.
--
local function wait_socket_state(uri, state)
    t.helpers.retrying({timeout = 60}, function()
        local conn = require("net.box").connect(uri)
        t.assert_equals(conn.state, state)
    end)
end

--
-- The string should be strictly in format: YEAR-MONTH-DAY HOUR:MIN:SEC.MS
--
local function test_only_one_record_appears_in_logs(server, record, wait_time)
    local first_log_record = nil
    t.helpers.retrying({timeout = 10}, function()
        first_log_record = server:grep_log(record)
        t.assert(first_log_record)
    end)
    -- We need to wait a bit in order to catch how much as possible
    -- spam in server's logs.
    require("fiber").sleep(wait_time)
    local last_log_record = server:grep_log(record)
    t.assert(last_log_record)
    t.assert_equals(first_log_record, last_log_record,
                    "There are two identical records in logs")
end

g.test_no_connection_refused_logs_when_connection_succeeds = function()
    g.storage:update_box_cfg({listen = "localhost:0"})
    local storage_uri = g.storage:exec(function()
        return box.info.listen[1]
    end)

    g.router:exec(function(storage_uri, reconnect_after)
        local net_box = require("net.box")
        local conn = net_box.connect(storage_uri,
                                     {reconnect_after = reconnect_after})
        t.assert(conn:is_connected())
    end, {storage_uri, g.reconnect_after})
    t.assert_not(g.router:grep_log("Connection refused"))
end

g.test_no_spam_in_logs_while_connecting_to_non_existent_instance = function()
    g.router:exec(function(reconnect_after)
        local netbox_f = require("fiber").create(function()
            require("net.box").connect("localhost:0",
                                       {reconnect_after = reconnect_after})
        end)
        rawset(_G, "netbox_f", netbox_f)
    end, {g.reconnect_after})
    test_only_one_record_appears_in_logs(g.router, g.connection_refused_pattern,
                                        g.reconnect_after * 2)
    g.router:exec(function() _G.netbox_f:cancel() end)
end

g.test_no_spam_in_logs_when_connection_cannot_be_restored_anymore = function()
    g.storage:update_box_cfg({listen = "localhost:0"})
    local storage_uri = g.storage:exec(function()
        return box.info.listen[1]
    end)
    g.router:exec(function(storage_uri, reconnect_after)
        local net_box = require("net.box")
        local conn = net_box.connect(storage_uri,
                                     {reconnect_after = reconnect_after})
        t.assert(conn:is_connected())
    end, {storage_uri, g.reconnect_after})
    g.storage:stop()
    test_only_one_record_appears_in_logs(g.router, g.connection_refused_pattern,
                                        g.reconnect_after * 2)
    g.storage:start()
end

g.test_no_spam_in_logs_during_complex_connection_scenarios = function()
    g.storage:update_box_cfg({listen = "localhost:0"})
    local storage_uri = g.storage:exec(function()
        return box.info.listen[1]
    end)
    g.storage:stop()
    -- Try to connect to non-existent storage. Should be only one
    -- "Connection refused" log.
    wait_socket_state(storage_uri, "error")
    g.router:exec(function(reconnect_after, storage_uri)
        local netbox_f = require("fiber").create(function()
            require("net.box").connect(storage_uri,
                                       {reconnect_after = reconnect_after})
        end)
        rawset(_G, "netbox_f", netbox_f)
    end, {g.reconnect_after, storage_uri})
    test_only_one_record_appears_in_logs(g.router, g.connection_refused_pattern,
                                         g.reconnect_after * 2)
    -- Restart our storage and bind the previous uri to it. Should not be any
    -- "Connection refused" logs. We check this by comparing "Connection refu
    -- sed" logs with datetimes before and after our storage reconfiguring.
    g.storage:start()
    local conn_refused_log_before_restart =
        g.router:grep_log(g.connection_refused_pattern)
    g.storage:update_box_cfg({listen = storage_uri})
    wait_socket_state(storage_uri, "active")
    local conn_refused_log_after_restart =
        g.router:grep_log(g.connection_refused_pattern)
    t.assert_equals(conn_refused_log_before_restart,
                    conn_refused_log_after_restart)
    -- Turn off the storage and check that there is only one
    -- "Connection refused" log after storage shut down.
    local conn_refused_before_shutdown =
        g.router:grep_log(g.connection_refused_pattern)
    g.storage:stop()
    wait_socket_state(storage_uri, "error")
    t.helpers.retrying({}, function()
        local conn_refused_after_shutdown =
            g.router:grep_log(g.connection_refused_pattern)
        t.assert_not_equals(conn_refused_before_shutdown,
                            conn_refused_after_shutdown)
    end)
    test_only_one_record_appears_in_logs(g.router, g.peer_closed_pattern,
                                        g.reconnect_after * 2)
    g.storage:start()
end
