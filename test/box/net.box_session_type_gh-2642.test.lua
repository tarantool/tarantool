net = require('net.box')

--
-- gh-2642: box.session.type()
--

box.schema.user.grant('guest','execute','universe')
c = net.connect(box.cfg.listen)
c:call("box.session.type")
c:close()
box.schema.user.revoke('guest', 'execute', 'universe')
