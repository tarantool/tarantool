remote = require 'net.box'
fiber = require 'fiber'
log = require 'log'
msgpack = require 'msgpack'

box.schema.user.grant('guest', 'read,write,execute', 'universe')
LISTEN = require('uri').parse(box.cfg.listen)
space = box.schema.create_space('net_box_test_space')
space:create_index('primary', { type = 'tree' })

-- low level connection
log.info("create connection")
cn = remote:new(LISTEN.host, LISTEN.service)
cn:_wait_state({'active', 'error'}, 1)
log.info("state is %s", cn.state)

cn:ping()
log.info("ping is done")
cn:ping()
log.info("ping is done")


cn:ping()


cn:call('unexists_procedure')

function test_foo(a,b,c) return { {{ [a] = 1 }}, {{ [b] = 2 }}, c } end

cn:call('test_foo', 'a', 'b', 'c')
cn:call(nil, 'a', 'b', 'c')

cn:_select(space.id, space.index.primary.id, 123)
space:insert{123, 345}
cn:_select(space.id, space.index.primary.id, 123)
cn:_select(space.id, space.index.primary.id, 123, { limit = 0 })
cn:_select(space.id, space.index.primary.id, 123, { limit = 1 })
cn:_select(space.id, space.index.primary.id, 123, { limit = 1, offset = 1 })

cn.space[space.id]  ~= nil
cn.space.net_box_test_space ~= nil
cn.space.net_box_test_space ~= nil
cn.space.net_box_test_space.index ~= nil
cn.space.net_box_test_space.index.primary ~= nil
cn.space.net_box_test_space.index[space.index.primary.id] ~= nil


cn.space.net_box_test_space.index.primary:select(123)
cn.space.net_box_test_space.index.primary:select(123, { limit = 0 })
cn.space.net_box_test_space.index.primary:select(nil, { limit = 1, })
cn.space.net_box_test_space:insert{234, 1,2,3}
cn.space.net_box_test_space:insert{234, 1,2,3}
cn.space.net_box_test_space.insert{234, 1,2,3}

cn.space.net_box_test_space:replace{354, 1,2,3}
cn.space.net_box_test_space:replace{354, 1,2,4}

cn.space.net_box_test_space:select{123}
space:select({123}, { iterator = 'GE' })
cn.space.net_box_test_space:select({123}, { iterator = 'GE' })
cn.space.net_box_test_space:select({123}, { iterator = 'GT' })
cn.space.net_box_test_space:select({123}, { iterator = 'GT', limit = 1 })
cn.space.net_box_test_space:select({123}, { iterator = 'GT', limit = 1, offset = 1 })

cn.space.net_box_test_space:select{123}
cn.space.net_box_test_space:update({123}, { { '+', 2, 1 } })
cn.space.net_box_test_space:update(123, { { '+', 2, 1 } })
cn.space.net_box_test_space:select{123}

cn.space.net_box_test_space:update({123}, { { '=', 1, 2 } })
cn.space.net_box_test_space:select{2}
cn.space.net_box_test_space:select({234}, { iterator = 'LT' })

cn.space.net_box_test_space:update({1}, { { '+', 2, 2 } })

cn.space.net_box_test_space:delete{1}
cn.space.net_box_test_space:delete{2}
cn.space.net_box_test_space:delete{2}

-- test one-based indexing in splice operation (see update.test.lua)
cn.space.net_box_test_space:replace({10, 'abcde'})
cn.space.net_box_test_space:update(10,  {{':', 2, 0, 0, '!'}})
cn.space.net_box_test_space:update(10,  {{':', 2, 1, 0, '('}})
cn.space.net_box_test_space:update(10,  {{':', 2, 2, 0, '({'}})
cn.space.net_box_test_space:update(10,  {{':', 2, -1, 0, ')'}})
cn.space.net_box_test_space:update(10,  {{':', 2, -2, 0, '})'}})
cn.space.net_box_test_space:delete{10}

cn.space.net_box_test_space:select({}, { iterator = 'ALL' })

cn.space.net_box_test_space.index.primary:min()
cn.space.net_box_test_space.index.primary:min(354)
cn.space.net_box_test_space.index.primary:max()
cn.space.net_box_test_space.index.primary:max(234)
cn.space.net_box_test_space.index.primary:count()
cn.space.net_box_test_space.index.primary:count(354)

cn.space.net_box_test_space:get(354)

-- reconnects after errors

-- -- 1. no reconnect
cn:_fatal('Test fatal error')
cn.state
cn:ping()
cn:call('test_foo')

-- -- 2 reconnect
cn = remote:new(LISTEN.host, LISTEN.service, { reconnect_after = .1 })
cn:_wait_state({'active'}, 1)
cn.space ~= nil

cn.space.net_box_test_space:select({}, { iterator = 'ALL' })
cn:_fatal 'Test error'
cn:_wait_state({'active', 'activew'}, 2)
cn:ping()
cn.state
cn.space.net_box_test_space:select({}, { iterator = 'ALL' })

cn:_fatal 'Test error'
cn:_select(space.id, 0, {}, { iterator = 'ALL' })

-- send broken packet (remote server will close socket)
cn.s:syswrite(msgpack.encode(1) .. msgpack.encode('abc'))
fiber.sleep(.2)

cn.state
cn:ping()

-- -- dot-new-method

cn1 = remote.new(LISTEN.host, LISTEN.service)
cn1:_select(space.id, 0, {}, { iterator = 'ALL' })

-- -- error while waiting for response
type(fiber.create(function() fiber.sleep(.5) cn:_fatal('Test error') end))
function pause() fiber.sleep(10) return true end

cn:call('pause')
cn:call('test_foo', 'a', 'b', 'c')


-- call
remote.self:call('test_foo', 'a', 'b', 'c')
cn:call('test_foo', 'a', 'b', 'c')

-- long replies
function long_rep() return { 1,  string.rep('a', 5000) } end
res = cn:call('long_rep')
res[1][1] == 1
res[1][2] == string.rep('a', 5000)

function long_rep() return { 1,  string.rep('a', 50000) } end
res = cn:call('long_rep')
res[1][1] == 1
res[1][2] == string.rep('a', 50000)

-- auth
cn.proto.b64decode('gJLocxbO32VmfO8x04xRVxKfgwzmNVM2t6a1ME8XsD0=')
cn.proto.b64decode('gJLoc!!!!!!!')

cn = remote:new(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123', wait_connected = true })
cn:is_connected()
cn.error
cn.state

box.schema.user.create('netbox', { password  = 'test' })
box.schema.user.grant('netbox', 'read, write, execute', 'universe');

cn = remote:new(LISTEN.host, LISTEN.service, { user = 'netbox', password = 'test' })
cn.state
cn.error
cn:ping()

function ret_after(to) fiber.sleep(to) return {{to}} end

-- timeouts
cn:timeout(1).space.net_box_test_space.index.primary:select{234}
cn:call('ret_after', .01)
cn:timeout(1):call('ret_after', .01)
cn:timeout(.01):call('ret_after', 1)

cn = remote:timeout(0.0000000001):new(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123' })
cn = remote:timeout(1):new(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123' })




remote.self:ping()
remote.self.space.net_box_test_space:select{234}
remote.self:timeout(123).space.net_box_test_space:select{234}



-- cleanup database after tests
space:drop()

