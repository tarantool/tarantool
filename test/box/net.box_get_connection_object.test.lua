net = require('net.box')

--
-- Check that it's possible to get connection object form net.box space
--

space = box.schema.space.create('test', {format={{name="id", type="unsigned"}}})
space ~= nil
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest','read,write,execute','space', 'test')

c = net.connect(box.cfg.listen)

c:ping()
c.space.test ~= nil

c.space.test.connection == c
box.schema.user.revoke('guest','read,write,execute','space', 'test')
c:close()
