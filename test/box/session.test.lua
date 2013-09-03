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
box.session.on_connect(function() end)
box.session.on_disconnect(function() end)

-- check it''s possible to reset these triggers
type(box.session.on_connect(function() error('hear') end))
type(box.session.on_disconnect(function() error('hear') end))

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
type(box.session.on_connect(nil))
type(box.session.on_disconnect(nil))
type(box.session.on_connect(nil))
type(box.session.on_disconnect(nil))

-- check how connect/disconnect triggers work
function inc() active_connections = active_connections + 1 end
function dec() active_connections = active_connections - 1 end
box.session.on_connect(inc)
box.session.on_disconnect(dec)
active_connections = 0
--# create connection con1 to default
--# set connection con1
active_connections
--# create connection con2 to default
--# set connection con2
active_connections
--# drop connection con1
--# drop connection con2

--# set connection default
type(box.session.on_connect(nil))
type(box.session.on_disconnect(nil))

-- write audit trail of connect/disconnect into a space
box.session.on_connect(function() box.insert(0, box.session.id()) end)
box.session.on_disconnect(function() box.delete(0, box.session.id()) end)

--# create connection con3 to default 
--# set connection con3
box.unpack('i', box.select(0, 0, box.session.id())[0]) == box.session.id()
--# drop connection con3

-- cleanup
type(box.session.on_connect(nil))
type(box.session.on_disconnect(nil))
active_connections
