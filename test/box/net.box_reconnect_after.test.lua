fiber = require 'fiber'
net = require('net.box')

box.schema.user.grant('guest', 'execute', 'universe')

--
-- Test a case, when netbox can not connect first time, but
-- reconnect_after is set.
--
c = net.connect('localhost:33333', {reconnect_after = 0.1, wait_connected = false})
while c.state ~= 'error_reconnect' do fiber.sleep(0.01) end
c:close()

box.schema.user.revoke('guest', 'execute', 'universe')
c.state
c = nil
