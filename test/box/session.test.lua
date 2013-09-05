box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')
box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')

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

-- check it's possible to reset these triggers
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
--# create connection con_one to default
active_connections
--# create connection con_two to default
active_connections
--# drop connection con_one
--# drop connection con_two
box.fiber.sleep(0) -- yield
active_connections

type(box.session.on_connect(nil))
type(box.session.on_disconnect(nil))

-- write audit trail of connect/disconnect into a space
box.session.on_connect(function() box.insert(0, box.session.id()) end)
box.session.on_disconnect(function() box.delete(0, box.session.id()) end)

--# create connection con_three to default 
--# set connection con_three
box.unpack('i', box.select(0, 0, box.session.id())[0]) == box.session.id()
--# set connection default
--# drop connection con_three

-- cleanup
type(box.session.on_connect(nil))
type(box.session.on_disconnect(nil))
active_connections

box.space[0]:drop()
