session = box.session
fiber = require('fiber')

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

session.exists(session.id())
session.peer(session.id())
session.exists()
session.exists(1, 2, 3)
session.exists(1234567890)

-- check session.id()
session.id() > 0
failed = false
f = fiber.create(function() failed = session.id() == 0 end)
while f:status() ~= 'dead' do fiber.sleep(0) end
failed
session.peer() == session.peer(session.id())

-- check on_connect/on_disconnect triggers
function noop() end
type(session.on_connect(noop))
type(session.on_disconnect(noop))

-- check it's possible to reset these triggers
function fail() error('hear') end
type(session.on_connect(fail, noop))
type(session.on_disconnect(fail, noop))

-- check on_connect/on_disconnect argument count and type
type(session.on_connect())
type(session.on_disconnect())

session.on_connect(function() end, function() end)
session.on_disconnect(function() end, function() end)

session.on_connect(1, 2)
session.on_disconnect(1, 2)

session.on_connect(1)
session.on_disconnect(1)

-- use of nil to clear the trigger
session.on_connect(nil, fail)
session.on_disconnect(nil, fail)

-- check how connect/disconnect triggers work
function inc() active_connections = active_connections + 1 end
function dec() active_connections = active_connections - 1 end
net = { box = require('net.box') }
type(session.on_connect(inc))
type(session.on_disconnect(dec))
active_connections = 0
LISTEN = require('uri').parse(box.cfg.listen)
c = net.box.connect(LISTEN.host, LISTEN.service)
while active_connections < 1 do fiber.sleep(0.001) end
active_connections
c1 = net.box.connect(LISTEN.host, LISTEN.service)
while active_connections < 2 do fiber.sleep(0.001) end
active_connections
c:close()
c1:close()
while active_connections > 0 do fiber.sleep(0.001) end
active_connections

session.on_connect(nil, inc)
session.on_disconnect(nil, dec)

-- write audit trail of connect/disconnect into a space
function audit_connect() box.space['tweedledum']:insert{session.id()} end
function audit_disconnect() box.space['tweedledum']:delete{session.id()} end
type(session.on_connect(audit_connect))
type(session.on_disconnect(audit_disconnect))

box.schema.user.grant('guest', 'read,write,execute', 'universe')
a = net.box.connect(LISTEN.host, LISTEN.service)
a:eval('return space:get{session.id()}[1] == session.id()')
a:eval('return session.sync() ~= 0')
a:close()

-- cleanup
session.on_connect(nil, audit_connect)
session.on_disconnect(nil, audit_disconnect)
active_connections

space:drop()

session.uid()
session.user()
session.sync()
fiber = nil
session = nil
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

-- audit permission in on_connect/on_disconnect triggers
box.schema.user.create('tester', { password = 'tester' })

on_connect_user = nil
on_disconnect_user = nil
function on_connect() on_connect_user = box.session.user() end
function on_disconnect() on_disconnect_user = box.session.user() end
_ = box.session.on_connect(on_connect)
_ = box.session.on_disconnect(on_disconnect)

conn = require('net.box').connect('tester:tester@'..box.cfg.listen)
-- Triggers must not lead to privilege escalation
conn:eval('box.space._user:select()')
conn:close()
conn = nil

-- Triggers are executed with admin permissions
on_connect_user == 'admin'
on_disconnect_user == 'admin'

box.session.on_connect(nil, on_connect)
box.session.on_disconnect(nil, on_disconnect)
box.schema.user.drop('tester')
