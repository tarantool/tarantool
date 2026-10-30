remote = require 'net.box'

LISTEN = require('uri').parse(box.cfg.listen)
box.schema.user.create('netbox', { password  = 'test' })

-- #544 usage for remote[point]method
cn = remote.connect(LISTEN.host, LISTEN.service)

box.schema.user.grant('guest', 'execute', 'universe')
cn:close()
cn = remote.connect(LISTEN.host, LISTEN.service)
cn:eval('return true')
cn.eval('return true')

cn.ping()

cn:close()

remote.self:eval('return true')
remote.self.eval('return true')
box.schema.user.revoke('guest', 'execute', 'universe')
box.schema.user.drop('netbox')
