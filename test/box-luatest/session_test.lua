local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new({
        alias = 'session',
        box_cfg = {
            memtx_memory =  50 * 1024 * 1024,
        },
    })
    cg.server:start()
    cg.server:exec(function()
        local space = box.schema.space.create('tweedledum')
        space:create_index('primary', { type = 'hash' })
    end)
end)

g.after_each(function(cg)
    cg.server:drop()
end)

-- Check that Tarantool creates ADMIN session for #! script.
--test:ok(session.exists(session.id()), "session is created")
--test:isnil(session.peer(session.id()), "session.peer")
--test:ok(session.exists(), "session.exists")
--local _, err = pcall(session.exists, 1, 2, 3)
--test:is(tostring(err), "session.exists(sid): bad arguments", "exists bad args #2")
--test:ok(not session.exists(1234567890), "session doesn't exist")

g.test_check_session = function(cg)
    cg.server:exec(function()
        local fiber = require('fiber')
        local session = box.session
        t.assert_gt(session.id(), 0, "id > 0")
        local failed = false
        local f = fiber.create(function() failed = session.id() == 0 end)
        while f:status() ~= 'dead' do fiber.sleep(0) end
        t.assert(not failed, 'session not broken')
        t.assert_equals(session.peer(), session.peer(session.id()),
                        'peer() == peer(id())')
    end)
end

g.test_check_session_triggers = function(cg)
    cg.server:exec(function()
        local session = box.session
        local function
            noop()
        end

        -- Check `on_connect/on_disconnect` triggers.
        t.assert_type(session.on_connect(noop), 'function',
                      'type of trigger noop on_connect')
        t.assert_type(session.on_disconnect(noop), 'function',
                      'type of trigger noop on_disconnect')

        -- Check it's possible to reset these triggers.
        local function fail() error('hear') end
        t.assert_type(session.on_connect(fail, noop), 'function',
                      'type of trigger fail, noop on_connect')
        t.assert_type(session.on_disconnect(fail, noop), 'function',
                      'type of trigger fail, noop on_disconnect')

        -- Check on_connect/on_disconnect argument count and type.
        t.assert_type(session.on_connect(), 'table',
                      'type of trigger on_connect, no args')
        t.assert_type(session.on_disconnect(), 'table',
                      'type of trigger on_disconnect, no args')

        local _, err = pcall(session.on_connect, function() end, function() end)
        t.assert_str_contains(err, 'trigger reset: Trigger is not found',
                              'on_connect trigger not found')
        _, err = pcall(session.on_disconnect, function() end, function() end)
        t.assert_str_contains(err, 'trigger reset: Trigger is not found',
                              'on_disconnect trigger not found')

        _, err = pcall(session.on_connect, 1, 2)
        t.assert_str_contains(err, 'trigger reset: incorrect arguments',
                              'on_connect bad args #1')
        _, err = pcall(session.on_disconnect, 1, 2)
        t.assert_str_contains(err, 'trigger reset: incorrect arguments',
                              'on_disconnect bad args #1')

        _, err = pcall(session.on_connect, 1)
        t.assert_str_contains(err, 'trigger reset: incorrect arguments',
                              'on_connect bad args #2')
        _, err = pcall(session.on_disconnect, 1)
        t.assert_str_contains(err, 'trigger reset: incorrect arguments',
                              'on_disconnect bad args #2')

        -- Use `nil` to clear the trigger.
        session.on_connect(nil, fail)
        session.on_disconnect(nil, fail)
    end)
end

-- Check how connect/disconnect triggers work.
g.test_session_triggers = function(cg)
    t.skip('TODO')
    local uri = cg.server.net_box_uri
    assert(uri)
    cg.server:exec(function()
        local session = box.session
        local active_connections = 0
        local function inc() active_connections = active_connections + 1 end
        local function dec() active_connections = active_connections - 1 end
        local net = { box = require('net.box') }
        t.assert_type(session.on_connect(inc), 'function',
                      'type of trigger inc on_connect')
        t.assert_type(session.on_disconnect(dec), 'function',
                      'type of trigger dec on_disconnect')
        local net_box = require('net.box')
        local fiber = require('fiber')
        local c = net_box.connect(uri)
        while active_connections < 1 do fiber.sleep(0.001) end
        t.assert_equals(active_connections, 1,
                        'active_connections after 1 connection')
        local net_box = require('net.box')
        local c1 = net_box.connect(uri)
        while active_connections < 2 do fiber.sleep(0.001) end
        t.assert_equals(active_connections, 2,
                        'active_connections after 2 connection')
        c:close()
        c1:close()
        while active_connections > 0 do fiber.sleep(0.001) end
        t.assert_equals(active_connections, 0,
                        'active_connections after closing')

        session.on_connect(nil, inc)
        session.on_disconnect(nil, dec)

        -- Write audit trail of connect/disconnect into a space.
        local function audit_connect()
            box.space['tweedledum']:insert{session.id()}
        end
        local function audit_disconnect()
            box.space['tweedledum']:delete{session.id()}
        end
        t.assert_type(session.on_connect(audit_connect), 'function',
                      'type of trigger audit_connect on_connect')
        t.assert_type(session.on_disconnect(audit_disconnect), 'function',
                      'type of trigger audit_connect on_disconnect')

        box.schema.user.grant('guest', 'read,write', 'space', 'tweedledum')
        box.schema.user.grant('guest', 'execute', 'universe')
        local conn = net_box.connect(uri)
        local res = conn:eval('return space:get{box.session.id()}[1] == session.id()')
        t.assert(res, 'eval get_id')
        res = conn:eval('return session.sync() ~= 0')
        t.assert(res, "eval sync")
        conn:close()

        -- Cleanup.
        session.on_connect(nil, audit_connect)
        session.on_disconnect(nil, audit_disconnect)
        t.assert(active_connections, 0, "active connections after other triggers")
    end)
end

g.test_type_session_triggers = function(cg)
    t.skip('TODO')
    local uri = cg.server.net_box_uri
    cg.server:exec(function()
        local session = box.session

        t.assert_equals(session.uid(), 1, 'uid == 1')
        t.assert_equals(session.user(), 'admin', 'user is admin')
        t.assert_equals(session.sync(), 0, 'sync constant')
        box.schema.user.revoke('guest', 'execute', 'universe')

        -- Audit permission in on_connect/on_disconnect triggers.
        box.schema.user.create('tester', { password = 'tester' })

        local on_connect_user = nil
        local on_disconnect_user = nil
        local function on_connect()
            on_connect_user = box.session.effective_user()
        end
        local function on_disconnect()
            on_disconnect_user = box.session.effective_user()
        end
        local _ = box.session.on_connect(on_connect)
        _ = box.session.on_disconnect(on_disconnect)
        local fiber = require('fiber')
        local net_box = require('net.box')
        local conn = net_box.connect(uri)
        -- Triggers must not lead to privilege escalation.
        local ok = pcall(function () conn:eval('box.space._user:select()') end)
        t.assert(not ok, 'check access')
        conn:close()
        conn = nil
        while not on_disconnect_user do fiber.sleep(0.001) end
        -- Triggers are executed with admin permissions.
        t.assert_equals(on_connect_user, 'admin',
                        'check trigger permissions, on_connect')
        t.assert_equals(on_disconnect_user, 'admin',
                        'check trigger permissions, on_disconnect')

        box.session.on_connect(nil, on_connect)
        box.session.on_disconnect(nil, on_disconnect)
    end)
end

-- Check session privilege.
g.test_session_priveleges = function(cg)
    t.skip('TODO')
    local uri = cg.server.net_box_uri
    cg.server:exec(function()
        local net_box = require('net.box')
        -- local session = box.session
        local ok = pcall(function()
            net_box.connect(uri)
        end)
        t.assert(ok, 'session privilege')
        box.schema.user.revoke('tester', 'session', 'universe')
        local conn = net_box.connect(uri)
        t.assert(conn.state, 'error', 'session privilege state')
        t.assert(conn.error:match('Session'), 'sesssion privilege errmsg')
        ok = pcall(box.session.su, 'user1')
        t.assert(not ok, 'session.su on revoked')
        box.schema.user.drop('tester')
    end)
end

g.test_session_gh_2994 = function()
    t.skip('TODO')
    --local uri = inspector:eval('session', 'box.cfg.listen')[1]
    --conn = net.box.connect(uri)
    --test:ok(conn:eval("return box.session.exists(box.session.id())"), "remote session exist check")
    --test:isnt(conn:eval("return box.session.peer(box.session.id())"), nil, "remote session peer check")
    --test:ok(conn:eval("return box.session.peer() == box.session.peer(box.session.id())"), "remote session peer check")
    --test:ok(conn:eval("return box.session.fd() == box.session.fd(box.session.id())"), "remote session fd check")

    ---- gh-2994 session uid vs session effective uid
    --test:is(session.euid(), 1, "session.uid")
    --test:is(session.su("guest", session.uid), 1, "session.uid from su is admin")
    --test:is(session.su("guest", session.euid), 0, "session.euid from su is guest")
    --local id = conn:eval("return box.session.uid()")
    --test:is(id, 0, "session.uid from netbox")
    --id = conn:eval("return box.session.euid()")
    --test:is(id, 0, "session.euid from netbox")
    ----box.session.su("admin")
    --conn:eval("box.session.su(\"admin\", box.schema.create_space, \"sp1\")")
    --local sp = conn:eval("return box.space._space.index.name:get{\"sp1\"}[2]")
    --test:is(sp, 1, "effective ddl owner")
    --conn:close()
end

-- gh-3450: box.session.sync() becomes request local.
g.test_gh_3450 = function()
    t.skip('TODO')
    local fiber = require('fiber')
    local cond = fiber.cond()
    local sync1, sync2
    local started = 0
    local function f1() -- luacheck: no unused
        started = started + 1
        cond:wait()
        sync1 = box.session.sync()
    end
    local function f2() -- luacheck: no unused
        started = started + 1
        sync2 = box.session.sync()
        cond:signal()
    end
    box.schema.func.create('f1')
    box.schema.func.create('f2')
    box.schema.user.grant('guest', 'execute', 'function', 'f1')
    box.schema.user.grant('guest', 'execute', 'function', 'f2')
    local net_box = require('net.box')
    local fiber = require('fiber')
    local conn = net_box.connect(box.cfg.listen)
    t.assert(conn:ping(), 'connect to self')
    local _ = fiber.create(function() conn:call('f1') end)
    while started ~= 1 do fiber.sleep(0.01) end
    _ = fiber.create(function() conn:call('f2') end)
    while started ~= 2 do fiber.sleep(0.01) end
    t.assert_not_equals(sync1, sync2, 'session.sync() is request local')
    conn:close()
    box.schema.user.revoke('guest', 'execute', 'function', 'f1')
    box.schema.user.revoke('guest', 'execute', 'function', 'f2')
end
