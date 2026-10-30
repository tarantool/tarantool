-- clear statistics
env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

box.stat.net.SENT -- zero
box.stat.net.RECEIVED -- zero
box.stat.net.CONNECTIONS -- zero
box.stat.net.REQUESTS -- zero

space = box.schema.space.create('tweedledum')
box.schema.user.grant('guest', 'read', 'space', 'tweedledum')
index = space:create_index('primary', { type = 'hash' })

ch = require('fiber').channel(1)
function tweedledee() ch:get() end
box.schema.func.create('tweedledee')
box.schema.user.grant('guest', 'execute', 'function', 'tweedledee')
remote = require 'net.box'

LISTEN = require('uri').parse(box.cfg.listen)
cn = remote.connect(LISTEN.host, LISTEN.service)
cn1 = remote.connect(LISTEN.host, LISTEN.service)
cn2 = remote.connect(LISTEN.host, LISTEN.service)
cn3 = remote.connect(LISTEN.host, LISTEN.service)

cn.space.tweedledum:select() --small request

box.stat.net.SENT.total > 0
box.stat.net.RECEIVED.total > 0
box.stat.net.CONNECTIONS.total
box.stat.net.REQUESTS.total > 0
box.stat.net.CONNECTIONS.current
box.stat.net.REQUESTS.current

WAIT_COND_TIMEOUT = 10

cn1:close()
cn2:close()
test_run:wait_cond(function() return box.stat.net.CONNECTIONS.current == 2 end, WAIT_COND_TIMEOUT)
cn3:close()
test_run:wait_cond(function() return box.stat.net.CONNECTIONS.current == 1 end, WAIT_COND_TIMEOUT)

requests_total_saved = box.stat.net.REQUESTS.total
future1 = cn:call('tweedledee', {}, {is_async = true})
test_run:wait_cond(function() return box.stat.net.REQUESTS.current == 1 end, WAIT_COND_TIMEOUT)
future2 = cn:call('tweedledee', {}, {is_async = true})
test_run:wait_cond(function() return box.stat.net.REQUESTS.current == 2 end, WAIT_COND_TIMEOUT)
ch:put(true)
ch:put(true)
future1:wait_result()
future2:wait_result()
test_run:wait_cond(function() return box.stat.net.REQUESTS.current == 0 end, WAIT_COND_TIMEOUT)
box.stat.net.REQUESTS.total - requests_total_saved == 2

-- reset
box.stat.reset()
box.stat.net.SENT.total
box.stat.net.RECEIVED.total
box.stat.net.CONNECTIONS.total
box.stat.net.REQUESTS.total
box.stat.net.CONNECTIONS.current
box.stat.net.REQUESTS.current

box.schema.func.drop('tweedledee')
space:drop() -- tweedledum
cn:close()
