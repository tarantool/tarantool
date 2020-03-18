fiber = require 'fiber'
net = require('net.box')

--
-- gh-3629: netbox leaks when a connection is closed deliberately
-- and it has non-finished requests.
--
ready = false
ok = nil
err = nil
c = net:connect(box.cfg.listen)
function do_long() while not ready do fiber.sleep(0.01) end end
box.schema.func.create('do_long')
box.schema.user.grant('guest', 'execute', 'function', 'do_long')
f = fiber.create(function() ok, err = pcall(c.call, c, 'do_long') end)
while f:status() ~= 'suspended' do fiber.sleep(0.01) end
c:close()
ready = true
while not err do fiber.sleep(0.01) end
ok, err
