session = require('session')
fiber = require('fiber')

space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })

session.exists(session.id())
session.exists()
session.exists(1, 2, 3)
session.exists(1234567890)

-- check session.id()
session.id() > 0
f = fiber.create(function() failed = session.id() == 0 end)
while f:status() ~= 'dead' do fiber.sleep(0) end
failed
session.peer() == session.peer(session.id())

-- check on_connect/on_disconnect triggers
function noop() end
session.on_connect(noop)
session.on_disconnect(noop)

-- check it's possible to reset these triggers
function fail() error('hear') end
session.on_connect(fail, noop)
session.on_disconnect(fail, noop)

-- check on_connect/on_disconnect argument count and type
session.on_connect()
session.on_disconnect()

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
session.on_connect(inc)
session.on_disconnect(dec)
active_connections = 0
--# create connection con_one to default
active_connections
--# create connection con_two to default
active_connections
--# drop connection con_one
--# drop connection con_two
fiber.sleep(0) -- yield
active_connections

session.on_connect(nil, inc)
session.on_disconnect(nil, dec)

-- write audit trail of connect/disconnect into a space
function audit_connect() box.space['tweedledum']:insert{session.id()} end
function audit_disconnect() box.space['tweedledum']:delete{session.id()} end
session.on_connect(audit_connect)
session.on_disconnect(audit_disconnect)

box.schema.user.grant('guest', 'read,write,execute', 'universe')
a = (require 'net.box'):new('127.0.0.1', box.cfg.listen)
a:call('dostring', 'return space:get{session.id()}[1] == session.id()')[1][1]
a:close()

-- cleanup
session.on_connect(nil, audit_connect)
session.on_disconnect(nil, audit_disconnect)
active_connections

space:drop()

session.uid()
session.user()
fiber = nil
session = nil
