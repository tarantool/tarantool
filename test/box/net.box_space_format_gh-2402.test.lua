net = require('net.box')

--
-- gh-2402 net.box doesn't support space:format()
--

space = box.schema.space.create('test', {format={{name="id", type="unsigned"}}})
space ~= nil
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest', 'read', 'space', 'test')

c = net.connect(box.cfg.listen)

c:ping()
c.space.test ~= nil

format = c.space.test:format()

format[1] ~= nil
format[1].name == "id"
format[1].type == "unsigned"

c.space.test:format({})
