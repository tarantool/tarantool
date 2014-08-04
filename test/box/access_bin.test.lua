--
-- Access control tests which require a binary protocol
-- connection to the server
--
box.schema.user.grant('guest','read,write,execute','universe')
session = box.session
net = { box = require('net.box') }
c = net.box:new(0, box.cfg.listen)
c:call("dostring", "session.su('admin')")
c:call("dostring", "return session.user()")
c:close()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
