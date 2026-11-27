local t = require("luatest")
local net = require('net.box')
local fiber = require('fiber')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local instance_config = require('internal.config.instance_config')

local g = t.group()

local TIMEOUT = 0.3
local ALIVE_WAIT = TIMEOUT * 3

g.after_each(function()
    if g.cluster ~= nil then
        g.cluster:drop()
        g.cluster = nil
    end
end)

local function assert_closed_by_idle(conn)
    t.helpers.retrying({}, function()
        t.assert_equals(conn.state, 'error')
        t.assert_equals(tostring(conn.error), 'Peer closed')
    end)
end

local function connect(uri)
    local conn = net.connect(uri)
    t.assert_equals(conn.state, 'active')
    conn:ping()
    return conn
end

g.test_user_idle_timeout = function()
    local config = cbuilder:new()
        :set_global_option('session.users.guest.idle_timeout', TIMEOUT)
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    local conn = connect(g.cluster['i-001'].net_box_uri)
    assert_closed_by_idle(conn)
    conn:close()
end

g.test_zero_and_unset_stay_open = function()
    local config = cbuilder:new()
        :set_global_option('session.users.guest.idle_timeout', 0)
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    local conn = connect(g.cluster['i-001'].net_box_uri)
    fiber.sleep(ALIVE_WAIT)
    t.assert_equals(conn.state, 'active')
    conn:close()
end

g.test_reset_on_reload = function()
    local config = cbuilder:new()
        :set_global_option('session.users.guest.idle_timeout', TIMEOUT)
        :add_instance('i-001', {})
        :config()

    g.cluster = cluster:new(config)
    g.cluster:start()

    local conn = connect(g.cluster['i-001'].net_box_uri)
    assert_closed_by_idle(conn)
    conn:close()

    local config_2 = cbuilder:new(config)
        :set_global_option('session.users.guest.idle_timeout', nil)
        :config()
    g.cluster:reload(config_2)

    local conn_2 = connect(g.cluster['i-001'].net_box_uri)
    fiber.sleep(ALIVE_WAIT)
    t.assert_equals(conn_2.state, 'active')
    conn_2:close()
end

g.test_invalid_idle_timeout_rejected = function()
    -- A negative idle_timeout is rejected by the option validator.
    local iconfig = {
        session = {users = {guest = {idle_timeout = -1}}},
    }
    local err = '[instance_config] session.users.guest.idle_timeout: ' ..
                'must be a non-negative number, got -1'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)

    -- A non-number idle_timeout is rejected by the scalar type check.
    iconfig = {
        session = {users = {alice = {idle_timeout = 'x'}}},
    }
    err = '[instance_config] session.users.alice.idle_timeout: ' ..
          'Unexpected data for scalar "number": Expected "number", ' ..
          'got "string"'
    t.assert_error_msg_equals(err, function()
        instance_config:validate(iconfig)
    end)
end
