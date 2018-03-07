-- clear statistics
env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

box.stat.net.SENT -- zero
box.stat.net.RECEIVED -- zero

space = box.schema.space.create('tweedledum')
box.schema.user.grant('guest','read,write,execute','universe')
index = space:create_index('primary', { type = 'hash' })
remote = require 'net.box'

LISTEN = require('uri').parse(box.cfg.listen)
cn = remote.connect(LISTEN.host, LISTEN.service)

cn.space.tweedledum:select() --small request

box.stat.net.SENT.total > 0
box.stat.net.RECEIVED.total > 0
-- box.stat.net.EVENTS.total > 0
-- box.stat.net.LOCKS.total > 0

-- reset
box.stat.reset()
box.stat.net.SENT.total
box.stat.net.RECEIVED.total

space:drop()
cn:close()
box.schema.user.revoke('guest','read,write,execute','universe')
