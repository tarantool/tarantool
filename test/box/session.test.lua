space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash' })

box.session.exists(box.session.id())
box.session.exists()
box.session.exists(1, 2, 3)
box.session.exists(1234567890)

-- check session.id()
box.session.id() > 0
f = box.fiber.create(function() box.fiber.detach() failed = box.session.id() ~= 0 end)
box.fiber.resume(f)
failed
f1 = box.fiber.create(function() if box.session.id() == 0 then failed = true end end)
box.fiber.resume(f1)
failed
box.session.peer() == box.session.peer(box.session.id())

-- check on_connect/on_disconnect triggers
function noop() end
box.session.on_connect(noop)
box.session.on_disconnect(noop)

-- check it's possible to reset these triggers
function fail() error('hear') end
box.session.on_connect(fail, noop)
box.session.on_disconnect(fail, noop)

-- check on_connect/on_disconnect argument count and type
box.session.on_connect()
box.session.on_disconnect()

box.session.on_connect(function() end, function() end)
box.session.on_disconnect(function() end, function() end)

box.session.on_connect(1, 2)
box.session.on_disconnect(1, 2)

box.session.on_connect(1)
box.session.on_disconnect(1)

-- use of nil to clear the trigger
box.session.on_connect(nil, fail)
box.session.on_disconnect(nil, fail)

-- check how connect/disconnect triggers work
function inc() active_connections = active_connections + 1 end
function dec() active_connections = active_connections - 1 end
box.session.on_connect(inc)
box.session.on_disconnect(dec)
active_connections = 0
--# create connection con_one to default
active_connections
--# create connection con_two to default
active_connections
--# drop connection con_one
--# drop connection con_two
box.fiber.sleep(0) -- yield
active_connections

box.session.on_connect(nil, inc)
box.session.on_disconnect(nil, dec)

-- write audit trail of connect/disconnect into a space
function audit_connect() box.space['tweedledum']:insert{box.session.id()} end
function audit_disconnect() box.space['tweedledum']:delete{box.session.id()} end
box.session.on_connect(audit_connect)
box.session.on_disconnect(audit_disconnect)

--# create connection con_three to default
--# set connection con_three
space:get{box.session.id()}[0] == box.session.id()
--# set connection default
--# drop connection con_three

-- cleanup
box.session.on_connect(nil, audit_connect)
box.session.on_disconnect(nil, audit_disconnect)
active_connections

space:drop()

box.session.uid()
box.session.user()
