#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('session')
local fiber = require('fiber')

box.cfg{
    listen = os.getenv('LISTEN');
    log="tarantool.log";
}

local uri = require('uri').parse(box.cfg.listen)
local HOST, PORT = uri.host or 'localhost', uri.service
session = box.session
space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })

test:plan(54)

---
--- Check that Tarantool creates ADMIN session for #! script
---
test:ok(session.exists(session.id()), "session is created")
test:isnil(session.peer(session.id()), "session.peer")
test:ok(session.exists(), "session.exists")
local _, err = pcall(session.exists, 1, 2, 3)
test:is(err, "session.exists(sid): bad arguments", "exists bad args #2")
test:ok(not session.exists(1234567890), "session doesn't exist")

-- check session.id()
test:ok(session.id() > 0, "id > 0")
local failed = false
local f = fiber.create(function() failed = session.id() == 0 end)
while f:status() ~= 'dead' do fiber.sleep(0) end
test:ok(not failed, "session not broken")
test:is(session.peer(), session.peer(session.id()), "peer() == peer(id())")

-- check on_connect/on_disconnect triggers
local function noop() end
test:is(type(session.on_connect(noop)), "function", "type of trigger noop on_connect")
test:is(type(session.on_disconnect(noop)), "function", "type of trigger noop on_disconnect")

-- check it's possible to reset these triggers
local function fail() error('hear') end
test:is(type(session.on_connect(fail, noop)), "function", "type of trigger fail, noop on_connect")
test:is(type(session.on_disconnect(fail, noop)), "function", "type of trigger fail, noop on_disconnect")

-- check on_connect/on_disconnect argument count and type
test:is(type(session.on_connect()), "table", "type of trigger on_connect, no args")
test:is(type(session.on_disconnect()), "table", "type of trigger on_disconnect, no args")

_, err = pcall(session.on_connect, function() end, function() end)
test:is(err,"trigger reset: Trigger is not found", "on_connect trigger not found")
_, err = pcall(session.on_disconnect, function() end, function() end)
test:is(err,"trigger reset: Trigger is not found", "on_disconnect trigger not found")

_, err = pcall(session.on_connect, 1, 2)
test:is(err, "trigger reset: incorrect arguments", "on_connect bad args #1")
_, err = pcall(session.on_disconnect, 1, 2)
test:is(err, "trigger reset: incorrect arguments", "on_disconnect bad args #1")

_, err = pcall(session.on_connect, 1)
test:is(err, "trigger reset: incorrect arguments", "on_connect bad args #2")
_, err = pcall(session.on_disconnect, 1)
test:is(err, "trigger reset: incorrect arguments", "on_disconnect bad args #2")

-- use of nil to clear the trigger
session.on_connect(nil, fail)
session.on_disconnect(nil, fail)

-- check how connect/disconnect triggers work
local active_connections = 0
local function inc() active_connections = active_connections + 1 end
local function dec() active_connections = active_connections - 1 end
local net = { box = require('net.box') }
test:is(type(session.on_connect(inc)), "function", "type of trigger inc on_connect")
test:is(type(session.on_disconnect(dec)), "function", "type of trigger dec on_disconnect")
local c = net.box.connect(HOST, PORT)
while active_connections < 1 do fiber.sleep(0.001) end
test:is(active_connections, 1, "active_connections after 1 connection")
local c1 = net.box.connect(HOST, PORT)
while active_connections < 2 do fiber.sleep(0.001) end
test:is(active_connections, 2, "active_connections after 2 connection")
c:close()
c1:close()
while active_connections > 0 do fiber.sleep(0.001) end
test:is(active_connections, 0, "active_connections after closing")

session.on_connect(nil, inc)
session.on_disconnect(nil, dec)

-- write audit trail of connect/disconnect into a space
local function audit_connect() box.space['tweedledum']:insert{session.id()} end
local function audit_disconnect() box.space['tweedledum']:delete{session.id()} end
test:is(type(session.on_connect(audit_connect)), "function", "type of trigger audit_connect on_connect")
test:is(type(session.on_disconnect(audit_disconnect)), "function", "type of trigger audit_connect on_disconnect")

box.schema.user.grant('guest', 'read,write', 'space', 'tweedledum')
box.schema.user.grant('guest', 'execute', 'universe')
local a = net.box.connect(HOST, PORT)
test:ok(a:eval('return space:get{box.session.id()}[1] == session.id()'), "eval get_id")
test:ok(a:eval('return session.sync() ~= 0'), "eval sync")
a:close()

-- cleanup
session.on_connect(nil, audit_connect)
session.on_disconnect(nil, audit_disconnect)
test:is(active_connections, 0, "active connections after other triggers")

space:drop() -- tweedledum

test:is(session.uid(), 1, "uid == 1")
test:is(session.user(), "admin", "user is admin")
test:is(session.sync(), 0, "sync constant")
box.schema.user.revoke('guest', 'execute', 'universe')

-- audit permission in on_connect/on_disconnect triggers
box.schema.user.create('tester', { password = 'tester' })

local on_connect_user = nil
local on_disconnect_user = nil
local function on_connect() on_connect_user = box.session.effective_user() end
local function on_disconnect() on_disconnect_user = box.session.effective_user() end
_ = box.session.on_connect(on_connect)
_ = box.session.on_disconnect(on_disconnect)
local conn = require('net.box').connect("tester:tester@" ..HOST..':'..PORT)
-- Triggers must not lead to privilege escalation
local ok = pcall(function () conn:eval('box.space._user:select()') end)
test:ok(not ok, "check access")
conn:close()
conn = nil
while not on_disconnect_user do fiber.sleep(0.001) end
-- Triggers are executed with admin permissions
test:is(on_connect_user, 'admin', "check trigger permissions, on_connect")
test:is(on_disconnect_user, 'admin', "check trigger permissions, on_disconnect")

box.session.on_connect(nil, on_connect)
box.session.on_disconnect(nil, on_disconnect)

-- check Session privilege
ok = pcall(function() net.box.connect("tester:tester@" ..HOST..':'..PORT) end)
test:ok(ok, "session privilege")
box.schema.user.revoke('tester', 'session', 'universe')
conn = net.box.connect("tester:tester@" ..HOST..':'..PORT)
test:is(conn.state, "error", "session privilege state")
test:ok(conn.error:match("Session"), "sesssion privilege errmsg")
ok = pcall(box.session.su, "user1")
test:ok(not ok, "session.su on revoked")
box.schema.user.drop('tester')

local test_run = require('test_run')
local inspector = test_run.new()
test:is(
    inspector:cmd("create server session with script='box/tiny.lua'\n"),
    true, 'instance created'
)
test:is(
    inspector:cmd('start server session'),
    true, 'instance started'
)
local uri = inspector:eval('session', 'box.cfg.listen')[1]
conn = net.box.connect(uri)
test:ok(conn:eval("return box.session.exists(box.session.id())"), "remote session exist check")
test:isnt(conn:eval("return box.session.peer(box.session.id())"), nil, "remote session peer check")
test:ok(conn:eval("return box.session.peer() == box.session.peer(box.session.id())"), "remote session peer check")
test:ok(conn:eval("return box.session.fd() == box.session.fd(box.session.id())"), "remote session fd check")

-- gh-2994 session uid vs session effective uid
test:is(session.euid(), 1, "session.uid")
test:is(session.su("guest", session.uid), 1, "session.uid from su is admin")
test:is(session.su("guest", session.euid), 0, "session.euid from su is guest")
local id = conn:eval("return box.session.uid()")
test:is(id, 0, "session.uid from netbox")
id = conn:eval("return box.session.euid()")
test:is(id, 0, "session.euid from netbox")
--box.session.su("admin")
conn:eval("box.session.su(\"admin\", box.schema.create_space, \"sp1\")")
local sp = conn:eval("return box.space._space.index.name:get{\"sp1\"}[2]")
test:is(sp, 1, "effective ddl owner")
conn:close()

--
-- gh-3450: box.session.sync() becomes request local.
--
local cond = fiber.cond()
local sync1, sync2
local started = 0
function f1()
	started = started + 1
	cond:wait()
	sync1 = box.session.sync()
end
function f2()
	started = started + 1
	sync2 = box.session.sync()
	cond:signal()
end
box.schema.func.create('f1')
box.schema.func.create('f2')
box.schema.user.grant('guest', 'execute', 'function', 'f1')
box.schema.user.grant('guest', 'execute', 'function', 'f2')
conn = net.box.connect(box.cfg.listen)
test:ok(conn:ping(), 'connect to self')
_ = fiber.create(function() conn:call('f1') end)
while started ~= 1 do fiber.sleep(0.01) end
_ = fiber.create(function() conn:call('f2') end)
while started ~= 2 do fiber.sleep(0.01) end
test:isnt(sync1, sync2, 'session.sync() is request local')
conn:close()
box.schema.user.revoke('guest', 'execute', 'function', 'f1')
box.schema.user.revoke('guest', 'execute', 'function', 'f2')

inspector:cmd('stop server session with cleanup=1')
session = nil
os.exit(test:check() == true and 0 or -1)
