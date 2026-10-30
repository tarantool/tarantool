netbox = require('net.box')
--
-- gh-4593: net.box on_connect() and on_disconnect() were called
-- not in time.
--
--
-- on_disconnect() trigger should not be called if a connection
-- was refused even before it managed to become active.
--
disconnected_count = 0
connected_count = 0
box.schema.user.disable('guest')

function on_connect()                                           \
    connected_count = connected_count + 1                       \
end
function on_disconnect()                                        \
    disconnected_count = disconnected_count + 1                 \
end

c = netbox.connect(box.cfg.listen, {wait_connected = false})    \
c:on_disconnect(on_disconnect)                                  \
c:on_connect(on_connect)
c:wait_connected()
c.state
c.error

connected_count
disconnected_count
c:close()
connected_count
disconnected_count
box.schema.user.enable('guest')

--
-- on_connect() should not be called on schema update.
--
box.schema.user.grant('guest', 'read,write,execute,create', 'universe')
c = netbox.connect(box.cfg.listen, {wait_connected = false})    \
c:on_disconnect(on_disconnect)                                  \
c:on_connect(on_connect)
function create_space() box.schema.create_space('test') end
c:call('create_space')
connected_count
disconnected_count

c:close()
connected_count
disconnected_count

box.space.test:drop()
box.schema.user.revoke('guest', 'read,write,execute,create', 'universe')
