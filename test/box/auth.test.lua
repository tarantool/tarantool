session = box.session
fiber = require('fiber')

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })
box.schema.user.create('test', {password='pass'})
box.schema.user.grant('test', 'read,write,execute', 'universe')
box.schema.user.create('test2', {password=''})
box.schema.user.grant('test2', 'read,write,execute', 'universe')
box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- check how authentication trigger work
-- no args trigger
function auth_trigger() counter = counter + 1 end
-- get user name as argument
function auth_trigger2(user_name) msg = 'user ' .. user_name .. ' is there' end
net = { box = require('net.box') }

-- set trigger
handle = session.on_auth(auth_trigger)
-- check handle
type(handle)
-- check triggers list
#session.on_auth()
handle2 = session.on_auth(auth_trigger2)

msg = ''

LISTEN = require('uri').parse(box.cfg.listen)

-- check connection with authentication(counter must be incremented)
counter = 0
c = net.box:new('test:pass@' .. LISTEN.host .. ':' .. LISTEN.service)
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c = net.box:new('test2:@' .. LISTEN.host .. ':' .. LISTEN.service)
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c = net.box:new('test2@' .. LISTEN.host .. ':' .. LISTEN.service)
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c1 = net.box:new(LISTEN.host, LISTEN.service, {user='test2'})
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c1 = net.box:new('guest@' .. LISTEN.host .. ':' .. LISTEN.service)
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c1 = net.box:new('guest:@' .. LISTEN.host .. ':' .. LISTEN.service)
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c1 = net.box:new(LISTEN.host, LISTEN.service, {user='guest', password=''})
while counter < 1 do fiber.sleep(0.001) end
counter
msg

counter = 0
c1 = net.box:new(LISTEN.host, LISTEN.service, {user='guest'})
while counter < 1 do fiber.sleep(0.001) end
counter
msg


-- check guest connection without authentication(no increment)
counter = 0
c1 = net.box:new(LISTEN.host, LISTEN.service)
c1:ping()
counter

-- cleanup
c:close()
c1:close()
session.on_auth(nil, auth_trigger)
session.on_auth(nil, auth_trigger2)
session.on_auth()
space:drop()
session.uid()
session.user()
session.sync()
session = nil
fiber = nil
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('test', 'read,write,execute', 'universe')
box.schema.user.drop('test', { if_exists = true})
