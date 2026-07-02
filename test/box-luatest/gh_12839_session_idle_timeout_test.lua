local net = require('net.box')
local server = require('luatest.server')
local fiber = require('fiber')
local t = require('luatest')

local TIMEOUT = 0.1
local ALIVE_WAIT = TIMEOUT * 4

local function assert_closed_by_idle(conn)
    t.helpers.retrying({}, function()
        t.assert_equals(conn.state, 'error')
        t.assert_equals(tostring(conn.error), 'Peer closed')
    end)
end

local g = t.group('session_idle_timeout')

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.schema.user.create('bob', {if_not_exists = true})
        box.schema.user.passwd('bob', 'secret')
        box.schema.user.grant('bob', 'execute', 'universe',
                              nil, {if_not_exists = true})
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.internal.session.idle_timeout_reset()
        for _, name in ipairs({'timed', 'fresh', 'revived', 'latecomer',
                               'quick', 'slow'}) do
            if box.schema.user.exists(name) then
                box.schema.user.drop(name)
            end
        end
    end)
end)

g.test_pre_auth_idle_close = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('guest', timeout)
    end, {TIMEOUT})
    local conn = net.connect(cg.server.net_box_uri)
    t.assert_equals(conn.state, 'active')
    conn:ping()
    assert_closed_by_idle(conn)
    conn:close()
end

g.test_activity_resets_timer = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('bob', timeout)
    end, {TIMEOUT})
    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'bob', password = 'secret'})
    t.assert_equals(conn.state, 'active')
    local deadline = fiber.time() + TIMEOUT * 2
    while fiber.time() < deadline do
        conn:ping()
        fiber.sleep(TIMEOUT / 3)
    end
    t.assert_equals(conn.state, 'active')
    conn:close()
end

g.test_reauth_after_idle_close = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('bob', timeout)
    end, {TIMEOUT})
    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'bob', password = 'secret'})
    t.assert_equals(conn.state, 'active')
    conn:ping()
    assert_closed_by_idle(conn)
    conn:close()

    conn = net.connect(cg.server.net_box_uri,
                       {user = 'bob', password = 'secret'})
    t.assert_equals(conn.state, 'active')
    t.assert(conn:ping())
    conn:close()
end

g.test_no_kill_during_request = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('bob', timeout)
    end, {TIMEOUT})
    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'bob', password = 'secret'})
    t.assert_equals(conn.state, 'active')
    conn:ping()
    local fut = conn:eval([[
        local fiber = require('fiber')
        fiber.sleep(...)
        return 'done'
    ]], {TIMEOUT * 5}, {is_async = true})
    fiber.sleep(TIMEOUT * 3)
    t.assert_equals(conn.state, 'active')
    local result = fut:wait_result(10)
    t.assert_equals(result[1], 'done')
    conn:close()
end

g.test_idle_timeout_is_per_user = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('bob', timeout)
    end, {TIMEOUT})

    local conn_guest = net.connect(cg.server.net_box_uri)
    t.assert_equals(conn_guest.state, 'active')
    conn_guest:ping()
    local conn_bob = net.connect(cg.server.net_box_uri,
                                 {user = 'bob', password = 'secret'})
    t.assert_equals(conn_bob.state, 'active')
    conn_bob:ping()

    assert_closed_by_idle(conn_bob)
    t.assert_equals(conn_guest.state, 'active')
    conn_bob:close()
    conn_guest:close()
end

g.test_disabled_timeout = function(cg)
    local conn = net.connect(cg.server.net_box_uri)
    t.assert_equals(conn.state, 'active')
    fiber.sleep(ALIVE_WAIT)
    t.assert_equals(conn.state, 'active')
    conn:close()
end

g.test_invalid_config_dynamic = function(cg)
    cg.server:exec(function()
        local set = box.internal.session.idle_timeout_set
        local prefix = "Incorrect value for option 'session_idle_timeout': "

        local details = "user name must be a string"
        t.assert_error_covers({
            type = "ClientError",
            code = box.error.CFG,
            name = "CFG",
            message = prefix .. details,
            details = details,
            option = "session_idle_timeout",
        }, set, 5, 5)

        details = "timeout must be a number"
        t.assert_error_covers({
            type = "ClientError",
            code = box.error.CFG,
            name = "CFG",
            message = prefix .. details,
            details = details,
            option = "session_idle_timeout",
        }, set, 'guest', 'x')


        details = "timeout must be >= 0"
        t.assert_error_covers({
            type = "ClientError",
            code = box.error.CFG,
            name = "CFG",
            message = prefix .. details,
            details = details,
            option = "session_idle_timeout",
        }, set, 'guest', -1)
    end)
    -- Rejected values must not have configured any timeout.
    local conn = net.connect(cg.server.net_box_uri)
    t.assert_equals(conn.state, 'active')
    conn:ping()
    fiber.sleep(ALIVE_WAIT)
    t.assert_equals(conn.state, 'active')
    conn:close()
end

-- Timeout is keyed by user name, not uid.
g.test_uid_reuse_does_not_inherit_timeout = function(cg)
    cg.server:exec(function(timeout)
        box.schema.user.create('timed')
        box.schema.user.passwd('timed', 'pw')
        box.schema.user.grant('timed', 'execute', 'universe')
        box.internal.session.idle_timeout_set('timed', timeout)
        box.schema.user.drop('timed')
        box.schema.user.create('fresh')
        box.schema.user.passwd('fresh', 'pw2')
        box.schema.user.grant('fresh', 'execute', 'universe')
    end, {TIMEOUT})

    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'fresh', password = 'pw2'})
    t.assert_equals(conn.state, 'active')
    conn:ping()
    fiber.sleep(ALIVE_WAIT)
    t.assert_equals(conn.state, 'active')
    conn:close()
end

g.test_recreate_user_reapplies_timeout = function(cg)
    cg.server:exec(function(timeout)
        box.schema.user.create('revived')
        box.schema.user.passwd('revived', 'pw3')
        box.schema.user.grant('revived', 'execute', 'universe')
        box.internal.session.idle_timeout_set('revived', timeout)
        box.schema.user.drop('revived')
        box.schema.user.create('revived')
        box.schema.user.passwd('revived', 'pw3')
        box.schema.user.grant('revived', 'execute', 'universe')
    end, {TIMEOUT})

    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'revived', password = 'pw3'})
    t.assert_equals(conn.state, 'active')
    conn:ping()
    assert_closed_by_idle(conn)
    conn:close()
end

g.test_timeout_applied_to_later_created_user = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('latecomer', timeout)
        box.schema.user.create('latecomer')
        box.schema.user.passwd('latecomer', 'pw6')
        box.schema.user.grant('latecomer', 'execute', 'universe')
    end, {TIMEOUT})

    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'latecomer', password = 'pw6'})
    t.assert_equals(conn.state, 'active')
    conn:ping()
    assert_closed_by_idle(conn)
    conn:close()
end

g.test_reauth_switches_timeout = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('quick', timeout)
        box.schema.user.create('quick')
        box.schema.user.passwd('quick', 'pw4')
        box.schema.user.grant('quick', 'execute', 'universe')
        box.schema.user.create('slow')
        box.schema.user.passwd('slow', 'pw5')
        box.schema.user.grant('slow', 'execute', 'universe')
    end, {TIMEOUT})

    local conn_quick = net.connect(cg.server.net_box_uri,
                                   {user = 'quick', password = 'pw4'})
    t.assert_equals(conn_quick.state, 'active')
    conn_quick:ping()
    assert_closed_by_idle(conn_quick)
    conn_quick:close()

    local conn_slow = net.connect(cg.server.net_box_uri,
                                  {user = 'slow', password = 'pw5'})
    t.assert_equals(conn_slow.state, 'active')
    conn_slow:ping()
    fiber.sleep(ALIVE_WAIT)
    t.assert_equals(conn_slow.state, 'active')
    conn_slow:close()
end

g.test_timer_starts_when_request_ends = function(cg)
    cg.server:exec(function(timeout)
        box.internal.session.idle_timeout_set('bob', timeout)
    end, {TIMEOUT})
    local conn = net.connect(cg.server.net_box_uri,
                             {user = 'bob', password = 'secret'})
    t.assert_equals(conn:eval([[
        local fiber = require('fiber')
        fiber.sleep(...)
        return 'done'
    ]], {TIMEOUT * 2}), 'done')
    local ended_at = fiber.clock()
    assert_closed_by_idle(conn)
    t.assert_ge(fiber.clock() - ended_at, TIMEOUT / 2)
    conn:close()
end
