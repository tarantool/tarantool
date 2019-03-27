-- clear statistics
env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

box.stat.net.SENT -- zero
box.stat.net.RECEIVED -- zero

space = box.schema.space.create('tweedledum')
box.schema.user.grant('guest', 'read', 'space', 'tweedledum')
index = space:create_index('primary', { type = 'hash' })
remote = require 'net.box'

LISTEN = require('uri').parse(box.cfg.listen)
cn = remote.connect(LISTEN.host, LISTEN.service)
cn1 = remote.connect(LISTEN.host, LISTEN.service)
cn2 = remote.connect(LISTEN.host, LISTEN.service)
cn3 = remote.connect(LISTEN.host, LISTEN.service)

cn.space.tweedledum:select() --small request

box.stat.net.SENT.total > 0
box.stat.net.RECEIVED.total > 0
box.stat.net.CONNECTIONS.current == 4
-- box.stat.net.EVENTS.total > 0
-- box.stat.net.LOCKS.total > 0

WAIT_COND_TIMEOUT = 10

cn1:close()
cn2:close()
test_run:wait_cond(function() return box.stat.net.CONNECTIONS.current == 2 end, WAIT_COND_TIMEOUT)
cn3:close()
test_run:wait_cond(function() return box.stat.net.CONNECTIONS.current == 1 end, WAIT_COND_TIMEOUT)

-- reset
box.stat.reset()
box.stat.net.SENT.total
box.stat.net.RECEIVED.total
box.stat.net.CONNECTIONS.current

space:drop() -- tweedledum
cn:close()
