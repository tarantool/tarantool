local server = require('luatest.server')
local t = require('luatest')
local net = require('net.box')

-- This test checks that all triggers are either raise an error or log it
-- with error log level
-- Transactional triggers are already tested in their own test suite

local g = t.group()

local function server_is_dead(s)
    return s == nil or s.process == nil or not s.process:is_alive()
end

-- Some tests can drop server so create it on demand
g.before_each(function()
    if server_is_dead(g.server) then
        g.server = server:new({box_cfg = {log_level = 'error'}})
        g.server:start()
    end
end)

g.after_all(function()
    g.server:drop()
end)

g.after_test('test_session_triggers_error', function()
    g.server:exec(function()
        box.session.on_connect(nil, nil, "test_trigger")
        box.session.on_disconnect(nil, nil, "test_trigger")
        box.session.on_auth(nil, nil, "test_trigger")
        box.session.on_access_denied(nil, nil, "test_trigger")
    end)
end)

g.test_session_triggers_error = function()
    g.server:exec(function()
        box.schema.user.create('user_without_rights', {password = 'password'})
        box.session.on_connect(function() error("on_connect error") end,
            nil, "test_trigger")
        box.session.on_disconnect(function() error("on_disconnect error") end,
            nil, "test_trigger")
        box.session.on_auth(function() error("on_auth error") end,
            nil, "test_trigger")

        t.assert_error_msg_content_equals("on_connect error",
            box.internal.session.run_on_connect)
        t.assert_error_msg_content_equals("on_auth error",
            box.internal.session.run_on_auth, "username", true)
        box.internal.session.run_on_disconnect()

        box.session.on_connect(nil, nil, "test_trigger")
        box.session.on_disconnect(nil, nil, "test_trigger")
        box.session.on_auth(nil, nil, "test_trigger")

        box.session.on_access_denied(
            function() error("on_access_denied error") end, nil, "test_trigger")
    end)

    -- Provoke on_access_denied error
    local conn = net.connect(g.server.net_box_uri, {
        user = 'user_without_rights', password = 'password',
        wait_connected = true
    })
    t.assert_equals(conn.error, nil)
    local ok = pcall(conn.call, conn, 'func')
    t.assert_not(ok)

    t.assert(g.server:grep_log("on_access_denied error"))
    t.assert(g.server:grep_log("on_disconnect error"))
end

g.after_test('test_ctl_triggers_error', function()
    if server_is_dead(g.server) then
        return
    end
    g.server:exec(function()
        box.ctl.on_schema_init(nil, nil, "test_trigger")
        box.ctl.on_shutdown(nil, nil, "test_trigger")
    end)
end)

g.test_ctl_triggers_error = function()
    local run_before_cfg =
        'box.ctl.on_schema_init(' ..
            'function() error("on_init error") end, nil, "test_trigger")'
    g.server:restart({
        box_cfg = {log_level = 'error'},
        env = {
            ['TARANTOOL_RUN_BEFORE_BOX_CFG'] = run_before_cfg,
        }
    })
    t.assert(g.server:grep_log("on_init error"))
    g.server:exec(function()
        box.ctl.on_shutdown(function() error("on_shutdown error") end, nil)
    end)

    local server_log_path = g.server:exec(function() return box.cfg.log end)
    g.server:drop()
    t.assert(g.server:grep_log("on_shutdown error", nil,
        {filename = server_log_path}))
end

g.after_test('test_space_triggers_error', function()
    g.server:exec(function()
        if box.space.test ~= nil then
            box.space.test:drop()
        end
    end)
end)

g.test_space_triggers_error = function()
    g.server:exec(function()
        local errmsg = 'space trigger error'
        local function trigger_f()
            error(errmsg)
        end
        local s = box.schema.create_space('test')
        s:create_index('pk')

        s:on_replace(trigger_f, nil, 'test')
        t.assert_error_msg_content_equals(errmsg, s.replace, s, {0})
        s:on_replace(nil, nil, 'test')

        s:before_replace(trigger_f, nil, 'test')
        t.assert_error_msg_content_equals(errmsg, s.replace, s, {0})
        s:before_replace(nil, nil, 'test')
    end)
end

g.after_test('test_space_triggers_error', function()
    g.server:exec(function()
        local s = rawget(_G, 'swim_obj')
        if s ~= nil then
            s:delete()
        end
        rawset(_G, 'swim_obj', nil)
    end)
end)

g.test_swim_triggers_error = function()
    g.server:exec(function()
        local swim = require('swim')
        local function uuid(i)
            local min_valid_prefix = '00000000-0000-1000-8000-'
            if i < 10 then
                return min_valid_prefix..'00000000000'..tostring(i)
            end
            assert(i < 100)
            return min_valid_prefix..'0000000000'..tostring(i)
        end
        local function uri(port)
            port = port or 0
            return '127.0.0.1:'..tostring(port)
        end

        local s = swim.new({generation = 0})
        rawset(_G, 'swim_obj', s)

        s:on_member_event(function()
            error('swim on_member_event error')
        end)
        s:cfg{uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01}
    end)
    t.assert(g.server:grep_log("swim on_member_event error"))
end

g.before_test('test_net_box_triggers_error', function()
    g.client = server:new{alias = 'client', box_cfg = {log_level = 'error'}}
    g.client:start()
end)

g.after_test('test_net_box_triggers_error', function()
    g.client:drop()
    g.client = nil
end)

g.test_net_box_triggers_error = function()
    g.client:exec(function(uri)
        local net = require('net.box')
        local conn = net.connect(uri, {wait_connected = false})
        t.assert_equals(conn.state, 'initial')
        local errmsg = 'net.box on_connect error'
        conn:on_connect(function() error(errmsg) end)
        conn:wait_connected()
        -- Error is raised when the connection is used
        t.assert_error_msg_content_equals(errmsg, conn.call, conn, 'abc')

        conn = net.connect(uri, {wait_connected = true})
        errmsg = 'net.box on_disconnect error'
        -- Error is logged - it will be checked later
        conn:on_disconnect(function() error(errmsg) end)
        conn:close()

        conn = net.connect(uri, {wait_connected = false})
        t.assert_equals(conn.state, 'initial')
        errmsg = 'net.box on_schema_reload error'
        conn:on_schema_reload(function() error(errmsg) end)
        conn:wait_connected()
        -- Error is raised when the connection is used
        t.assert_error_msg_content_equals(errmsg, conn.call, conn, 'abc')

        conn = net.connect(uri, {wait_connected = true})
        errmsg = 'net.box on_shutdown error'
        -- Error is logged - it will be checked later
        conn:on_shutdown(function() error(errmsg) end)
    end, {g.server.net_box_uri})
    g.server:drop()

    t.assert(g.client:grep_log("net.box on_disconnect error"))
    t.assert(g.client:grep_log("net.box on_shutdown error"))
    g.client:drop()
end
