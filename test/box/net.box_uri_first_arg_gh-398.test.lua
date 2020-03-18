remote = require 'net.box'

LISTEN = require('uri').parse(box.cfg.listen)
box.schema.user.create('netbox', { password  = 'test' })

-- uri as the first argument
uri = string.format('%s:%s@%s:%s', 'netbox', 'test', LISTEN.host, LISTEN.service)

cn = remote.new(uri)
cn:ping()
cn:close()

uri = string.format('%s@%s:%s', 'netbox', LISTEN.host, LISTEN.service)
cn = remote.new(uri)
cn ~= nil, cn.state, cn.error
cn:close()
-- don't merge creds from uri & opts
remote.new(uri, { password = 'test' })
cn = remote.new(uri, { user = 'netbox', password = 'test' })
cn:ping()
cn:close()

box.schema.user.drop('netbox')
