net = require('net.box')

space = box.schema.space.create('test', {format={{name="id", type="unsigned"}}})
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest', 'read', 'space', 'test')

c = net.connect(box.cfg.listen)

--
-- gh-4091: index unique flag is always false.
--
c.space.test.index.primary.unique

c:close()
space:drop()
