fiber = require 'fiber'
net = require('net.box')

--
-- gh-2677: netbox supports console connections, that complicates
-- both console and netbox. It was necessary because before a
-- connection is established, a console does not known is it
-- binary or text protocol, and netbox could not be created from
-- existing socket.
--
box.schema.user.grant('guest', 'execute', 'universe')
urilib = require('uri')
uri = urilib.parse(tostring(box.cfg.listen))
s, greeting = net.establish_connection(uri.host, uri.service)
c = net.wrap(s, greeting, uri.host, uri.service, {reconnect_after = 0.01})
c.state

a = 100
function kek(args) return {1, 2, 3, args} end
c:eval('a = 200')
a
c:call('kek', {300})
s = box.schema.create_space('test')
box.schema.user.grant('guest', 'read,write', 'space', 'test')
pk = s:create_index('pk')
c:reload_schema()
c.space.test:replace{1}
c.space.test:get{1}
c.space.test:delete{1}
--
-- Break a connection to test reconnect_after.
--
_ = c._transport.perform_request(nil, nil, false, net._method.inject, nil, nil, nil, nil, '\x80')
while not c:is_connected() do fiber.sleep(0.01) end
c:ping()

s:drop()
c:close()
box.schema.user.revoke('guest', 'execute', 'universe')
