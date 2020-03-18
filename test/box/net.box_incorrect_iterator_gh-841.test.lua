remote = require 'net.box'
fiber = require 'fiber'
test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")

test_run:cmd("setopt delimiter ';'")
function x_select(cn, space_id, index_id, iterator, offset, limit, key, opts)
    local ret = cn:_request('select', opts, nil, space_id, index_id, iterator,
                            offset, limit, key)
    return ret
end
function x_fatal(cn) cn._transport.perform_request(nil, nil, false, 'inject', nil, nil, nil, '\x80') end
test_run:cmd("setopt delimiter ''");

LISTEN = require('uri').parse(box.cfg.listen)
space = box.schema.space.create('net_box_test_space')
index = space:create_index('primary', { type = 'tree' })

function test_foo(a,b,c) return { {{ [a] = 1 }}, {{ [b] = 2 }}, c } end

box.schema.user.grant('guest', 'read,write', 'space', 'net_box_test_space')
box.schema.user.grant('guest', 'execute', 'universe')

cn = remote.connect(box.cfg.listen)

x_select(cn, space.id, space.index.primary.id, box.index.EQ, 0, 0xFFFFFFFF, 123)
space:insert{123, 345}
x_select(cn, space.id, space.index.primary.id, box.index.EQ, 0, 0, 123)
x_select(cn, space.id, space.index.primary.id, box.index.EQ, 0, 1, 123)
x_select(cn, space.id, space.index.primary.id, box.index.EQ, 1, 1, 123)

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

cn.space.net_box_test_space:insert(cn.space.net_box_test_space:get{123}:update{ { '=', 1, 2 } })
cn.space.net_box_test_space:delete{123}
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
-- gh-841: net.box uses incorrect iterator type for select with no arguments
cn.space.net_box_test_space:select()

cn.space.net_box_test_space.index.primary:min()
cn.space.net_box_test_space.index.primary:min(354)
cn.space.net_box_test_space.index.primary:max()
cn.space.net_box_test_space.index.primary:max(234)
cn.space.net_box_test_space.index.primary:count()
cn.space.net_box_test_space.index.primary:count(354)

cn.space.net_box_test_space:get(354)

-- reconnects after errors

box.schema.user.revoke('guest', 'execute', 'universe')
box.schema.func.create('test_foo')
box.schema.user.grant('guest', 'execute', 'function', 'test_foo')

-- -- 1. no reconnect
x_fatal(cn)
cn.state
cn:ping()
cn:call('test_foo')
cn:wait_state('active')

-- -- 2 reconnect
cn = remote.connect(LISTEN.host, LISTEN.service, { reconnect_after = .1 })
cn.space ~= nil

cn.space.net_box_test_space:select({}, { iterator = 'ALL' })
x_fatal(cn)
cn:wait_connected()
cn:wait_state('active')
cn:wait_state({active=true})
cn:ping()
cn.state
cn.space.net_box_test_space:select({}, { iterator = 'ALL' })

x_fatal(cn)
x_select(cn, space.id, 0, box.index.ALL, 0, 0xFFFFFFFF, {})

cn.state
cn:ping()

-- -- dot-new-method

cn1 = remote.new(LISTEN.host, LISTEN.service)
x_select(cn1, space.id, 0, box.index.ALL, 0, 0xFFFFFFF, {})
cn1:close()
-- -- error while waiting for response
type(fiber.create(function() fiber.sleep(.5) x_fatal(cn) end))
function pause() fiber.sleep(10) return true end

box.schema.func.create('pause')
box.schema.user.grant('guest', 'execute', 'function', 'pause')
cn:call('pause')
cn:call('test_foo', {'a', 'b', 'c'})
box.schema.func.drop('pause')

-- call
remote.self:call('test_foo', {'a', 'b', 'c'})
cn:call('test_foo', {'a', 'b', 'c'})
box.schema.func.drop('test_foo')

box.schema.func.create('long_rep')
box.schema.user.grant('guest', 'execute', 'function', 'long_rep')

-- long replies
function long_rep() return { 1,  string.rep('a', 5000) } end
res = cn:call('long_rep')
res[1] == 1
res[2] == string.rep('a', 5000)

function long_rep() return { 1,  string.rep('a', 50000) } end
res = cn:call('long_rep')
res[1] == 1
res[2] == string.rep('a', 50000)

box.schema.func.drop('long_rep')

-- a.b.c.d
u = '84F7BCFA-079C-46CC-98B4-F0C821BE833E'
X = {}
X.X = X
function X.fn(x,y) return y or x end
box.schema.user.grant('guest', 'execute', 'universe')
cn:close()
cn = remote.connect(LISTEN.host, LISTEN.service)
cn:call('X.fn', {u})
cn:call('X.X.X.X.X.X.X.fn', {u})
cn:call('X.X.X.X:fn', {u})
box.schema.user.revoke('guest', 'execute', 'universe')
cn:close()

-- auth

cn = remote.connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123', wait_connected = true })
cn:is_connected()
cn.error
cn.state
