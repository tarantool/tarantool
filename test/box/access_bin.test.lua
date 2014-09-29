--
-- Access control tests which require a binary protocol
-- connection to the server
--
box.schema.user.grant('guest','read,write,execute','universe')
session = box.session
net = { box = require('net.box') }
LISTEN = require('uri').parse(box.cfg.listen)
LISTEN ~= nil
c = net.box:new(LISTEN.host, LISTEN.service)
c:call("dostring", "session.su('admin')")
c:call("dostring", "return session.user()")
c:close()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

-- gh-488 suid functions
--
setuid_space = box.schema.space.create('setuid_space')
setuid_space:create_index('primary')
setuid_func = function() return box.space.setuid_space:auto_increment{} end
box.schema.func.create('setuid_func')
box.schema.user.grant('guest', 'execute', 'function', 'setuid_func')
c = net.box:new(LISTEN.host, LISTEN.service)
c:call("setuid_func")
session.su('guest')
setuid_func()
session.su('admin')
box.schema.func.drop('setuid_func')
box.schema.func.create('setuid_func', { setuid = true })
box.schema.user.grant('guest', 'execute', 'function', 'setuid_func')
c:call("setuid_func")
session.su('guest')
setuid_func()
session.su('admin')
c:close()
box.schema.func.drop('setuid_func')
setuid_space:drop()
--
