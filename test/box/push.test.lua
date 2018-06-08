--
-- gh-2677: box.session.push.
--

--
-- Usage.
--
box.session.push()
box.session.push(1, 2)

ok = nil
err = nil
function do_push() ok, err = box.session.push(1) end

--
-- Test binary protocol.
--
netbox = require('net.box')
box.schema.user.grant('guest', 'read,write,execute', 'universe')

c = netbox.connect(box.cfg.listen)
c:ping()
c:call('do_push')
ok, err
c:close()

box.schema.user.revoke('guest', 'read,write,execute', 'universe')

--
-- Ensure can not push in background.
--
fiber = require('fiber')
f = fiber.create(do_push)
while f:status() ~= 'dead' do fiber.sleep(0.01) end
ok, err
