remote = require 'net.box'
fiber = require 'fiber'
log = require 'log'

box.schema.user.grant('guest', 'read,write,execute', 'universe')
port = box.cfg.primary_port
space = box.schema.create_space('net_box_test_space')
space:create_index('primary', { type = 'tree' })

-- low level connection
log.info("create connection")
cn = remote:new('127.0.0.1', port)
cn:wait_state({'active', 'error'}, 1)
log.info("state is %s", cn.state)

cn:ping()
log.info("ping is done")
cn:ping()
log.info("ping is done")


cn:ping()


cn:call('unexists_procedure')

function test_foo(a,b,c) return { {{ [a] = 1 }}, {{ [b] = 2 }}, c } end

cn:call('test_foo', 'a', 'b', 'c')


cn:select(space.id, space.index.primary.id, 123)
space:insert{123, 345}
cn:select(space.id, space.index.primary.id, 123)
cn:select(space.id, space.index.primary.id, 123, { limit = 0 })
cn:select(space.id, space.index.primary.id, 123, { limit = 1 })
cn:select(space.id, space.index.primary.id, 123, { limit = 1, offset = 1 })

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

cn.space.net_box_test_space:replace{354, 1,2,3}
cn.space.net_box_test_space:replace{354, 1,2,4}

cn.space.net_box_test_space:select{123}
space:select({123}, { iterator = 'GE' })
cn.space.net_box_test_space:select({123}, { iterator = 'GE' })
cn.space.net_box_test_space:select({123}, { iterator = 'GT' })
cn.space.net_box_test_space:select({123}, { iterator = 'GT', limit = 1 })
cn.space.net_box_test_space:select({123}, { iterator = 'GT', limit = 1, offset = 1 })

cn.space.net_box_test_space:select{123}
cn.space.net_box_test_space:update({123}, { { '+', 1, 2 } })
cn.space.net_box_test_space:select{123}

cn.space.net_box_test_space:update({123}, { { '=', 0, 2 } })
cn.space.net_box_test_space:select{2}
cn.space.net_box_test_space:select({234}, { iterator = 'LT' })

cn.space.net_box_test_space:update({1}, { { '+', 1, 2 } })

cn.space.net_box_test_space:delete{1}
cn.space.net_box_test_space:delete{2}
cn.space.net_box_test_space:delete{2}

cn.space.net_box_test_space:select({}, { iterator = 'ALL' })


-- reconnects after errors

-- -- 1. no reconnect
cn:fatal('Test fatal error')
cn.state
cn:ping()
cn:call('test_foo')

-- -- 2 reconnect
cn = remote:new('127.0.0.1', port, { reconnect_after = .1 })
cn:wait_state({'active'}, 1)
cn.space ~= nil

cn.space.net_box_test_space:select({}, { iterator = 'ALL' })
cn:fatal 'Test error'
cn:wait_state({'active', 'activew'}, 2)
cn:ping()
cn.state
cn.space.net_box_test_space:select({}, { iterator = 'ALL' })

cn:fatal 'Test error'
cn:select(space.id, 0, {}, { iterator = 'ALL' })

-- -- error while waiting for response
type(fiber.wrap(function() fiber.sleep(.5) cn:fatal('Test error') end))
function pause() fiber.sleep(10) return true end

cn:call('pause')
cn:call('test_foo', 'a', 'b', 'c')


-- cleanup database after tests
space:drop()
