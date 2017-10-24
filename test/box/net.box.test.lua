remote = require 'net.box'
fiber = require 'fiber'
log = require 'log'
msgpack = require 'msgpack'
env = require('test_run')
test_run = env.new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")

test_run:cmd("setopt delimiter ';'")
function x_select(cn, space_id, index_id, iterator, offset, limit, key, opts)
    return cn:_request('select', opts, space_id, index_id, iterator,
                       offset, limit, key)
end
function x_fatal(cn) cn._transport.perform_request(nil, nil, 'inject', nil, '\x80') end
test_run:cmd("setopt delimiter ''");

LISTEN = require('uri').parse(box.cfg.listen)
space = box.schema.space.create('net_box_test_space')
index = space:create_index('primary', { type = 'tree' })

-- low level connection
log.info("create connection")
cn = remote.connect(LISTEN.host, LISTEN.service)
log.info("state is %s", cn.state)

cn:ping()
log.info("ping is done")
cn:ping()
log.info("ping is done")


cn:ping()


-- check permissions
cn:call('unexists_procedure')
function test_foo(a,b,c) return { {{ [a] = 1 }}, {{ [b] = 2 }}, c } end
cn:call('test_foo', {'a', 'b', 'c'})
cn:eval('return 2+2')

box.schema.user.grant('guest','execute','universe')
cn:close()
cn = remote.connect(box.cfg.listen)

cn:call('unexists_procedure')
cn:call('test_foo', {'a', 'b', 'c'})
cn:call(nil, {'a', 'b', 'c'})
cn:eval('return 2+2')
cn:eval('return 1, 2, 3')
cn:eval('return ...', {1, 2, 3})
cn:eval('return { k = "v1" }, true, {  xx = 10, yy = 15 }, nil')
cn:eval('return nil')
cn:eval('return')
cn:eval('error("exception")')
cn:eval('box.error(0)')
cn:eval('!invalid expression')

remote.self:eval('return 1+1, 2+2')
remote.self:eval('return')
remote.self:eval('error("exception")')
remote.self:eval('box.error(0)')
remote.self:eval('!invalid expression')

--
-- gh-822: net.box.call should roll back local transaction on error
--

_ = box.schema.space.create('gh822')
_ = box.space.gh822:create_index('primary')

test_run:cmd("setopt delimiter ';'")

-- rollback on invalid function
function rollback_on_invalid_function()
    box.begin()
    box.space.gh822:insert{1, "netbox_test"}
    pcall(remote.self.call, remote.self, 'invalid_function')
    return box.space.gh822:get(1) == nil
end;
rollback_on_invalid_function();

-- rollback on call error
function test_error() error('Some error') end;
function rollback_on_call_error()
    box.begin()
    box.space.gh822:insert{1, "netbox_test"}
    pcall(remote.self.call, remote.self, 'test_error')
    return box.space.gh822:get(1) == nil
end;
rollback_on_call_error();

-- rollback on eval
function rollback_on_eval_error()
    box.begin()
    box.space.gh822:insert{1, "netbox_test"}
    pcall(remote.self.eval, remote.self, "error('Some error')")
    return box.space.gh822:get(1) == nil
end;
rollback_on_eval_error();

test_run:cmd("setopt delimiter ''");
box.space.gh822:drop()

box.schema.user.revoke('guest','execute','universe')
box.schema.user.grant('guest','read,write,execute','universe')
cn:close()
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

cn:call('pause')
cn:call('test_foo', {'a', 'b', 'c'})


-- call
remote.self:call('test_foo', {'a', 'b', 'c'})
cn:call('test_foo', {'a', 'b', 'c'})

-- long replies
function long_rep() return { 1,  string.rep('a', 5000) } end
res = cn:call('long_rep')
res[1] == 1
res[2] == string.rep('a', 5000)

function long_rep() return { 1,  string.rep('a', 50000) } end
res = cn:call('long_rep')
res[1] == 1
res[2] == string.rep('a', 50000)

-- a.b.c.d
u = '84F7BCFA-079C-46CC-98B4-F0C821BE833E'
X = {}
X.X = X
function X.fn(x,y) return y or x end
cn:call('X.fn', {u})
cn:call('X.X.X.X.X.X.X.fn', {u})
cn:call('X.X.X.X:fn', {u})

-- auth

cn = remote.connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123', wait_connected = true })
cn:is_connected()
cn.error
cn.state

box.schema.user.create('netbox', { password  = 'test' })
box.schema.user.grant('netbox', 'read, write, execute', 'universe');

cn = remote.connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = 'test' })
cn.state
cn.error
cn:ping()

function ret_after(to) fiber.sleep(to) return {{to}} end

cn:ping({timeout = 1.00})
cn:ping({timeout = 1e-9})
cn:ping()

remote_space = cn.space.net_box_test_space
remote_pk = remote_space.index.primary

remote_space:insert({0}, { timeout = 1.00 })
remote_space:insert({1}, { timeout = 1e-9 })
remote_space:insert({2})

remote_space:replace({0}, { timeout = 1e-9 })
remote_space:replace({1})
remote_space:replace({2}, { timeout = 1.00 })

remote_space:upsert({3}, {}, { timeout = 1e-9 })
remote_space:upsert({4}, {})
remote_space:upsert({5}, {}, { timeout = 1.00 })
remote_space:upsert({3}, {})

remote_space:update({3}, {}, { timeout = 1e-9 })
remote_space:update({4}, {})
remote_space:update({5}, {}, { timeout = 1.00 })
remote_space:update({3}, {})

remote_pk:update({5}, {}, { timeout = 1e-9 })
remote_pk:update({4}, {})
remote_pk:update({3}, {}, { timeout = 1.00 })
remote_pk:update({5}, {})

remote_space:get({0})
remote_space:get({1}, { timeout = 1.00 })
remote_space:get({2}, { timeout = 1e-9 })

remote_pk:get({3}, { timeout = 1e-9 })
remote_pk:get({4})
remote_pk:get({5}, { timeout = 1.00 })

remote_space:select({2}, { timeout = 1e-9 })
remote_space:select({2}, { timeout = 1.00 })
remote_space:select({2})

remote_pk:select({2}, { timeout = 1.00 })
remote_pk:select({2}, { timeout = 1e-9 })
remote_pk:select({2})

remote_space:select({5}, { timeout = 1.00, iterator = 'LE', limit = 5 })
remote_space:select({5}, { iterator = 'LE', limit = 5})
remote_space:select({5}, { timeout = 1e-9, iterator = 'LE', limit = 5 })

remote_pk:select({2}, { timeout = 1.00, iterator = 'LE', limit = 5 })
remote_pk:select({2}, { iterator = 'LE', limit = 5})
remote_pk:select({2}, { timeout = 1e-9, iterator = 'LE', limit = 5 })

remote_pk:count({2}, { timeout = 1.00})
remote_pk:count({2}, { timeout = 1e-9})
remote_pk:count({2})

remote_pk:count({2}, { timeout = 1.00, iterator = 'LE' })
remote_pk:count({2}, { iterator = 'LE'})
remote_pk:count({2}, { timeout = 1e-9, iterator = 'LE' })

remote_pk:min(nil, { timeout = 1.00 })
remote_pk:min(nil, { timeout = 1e-9 })
remote_pk:min(nil)

remote_pk:min({0}, { timeout = 1e-9 })
remote_pk:min({1})
remote_pk:min({2}, { timeout = 1.00 })

remote_pk:max(nil)
remote_pk:max(nil, { timeout = 1e-9 })
remote_pk:max(nil, { timeout = 1.00 })

remote_pk:max({0}, { timeout = 1.00 })
remote_pk:max({1}, { timeout = 1e-9 })
remote_pk:max({2})

_ = remote_space:delete({0}, { timeout = 1e-9 })
_ = remote_pk:delete({0}, { timeout = 1.00 })
_ = remote_space:delete({1}, { timeout = 1.00 })
_ = remote_pk:delete({1}, { timeout = 1e-9 })
_ = remote_space:delete({2}, { timeout = 1e-9 })
_ = remote_pk:delete({2})
_ = remote_pk:delete({3})
_ = remote_pk:delete({4})
_ = remote_pk:delete({5})

remote_space:get(0)
remote_space:get(1)
remote_space:get(2)

remote_space = nil

cn:call('ret_after', {0.01}, { timeout = 1.00 })
cn:call('ret_after', {1.00}, { timeout = 1e-9 })

cn:eval('return ret_after(...)', {0.01}, { timeout = 1.00 })
cn:eval('return ret_after(...)', {1.00}, { timeout = 1e-9 })

--
-- :timeout()
-- @deprecated since 1.7.4
--

cn:timeout(1).space.net_box_test_space.index.primary:select{234}
cn:call('ret_after', {.01})
cn:timeout(1):call('ret_after', {.01})
cn:timeout(.01):call('ret_after', {1})

cn = remote:timeout(0.0000000001):connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123' })
cn:close()
cn = remote:timeout(1):connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123' })

remote.self:ping()
remote.self.space.net_box_test_space:select{234}
remote.self:timeout(123).space.net_box_test_space:select{234}
remote.self:is_connected()
remote.self:wait_connected()

cn:close()
-- cleanup database after tests
space:drop()

-- #1545 empty password
cn = remote.connect(LISTEN.host, LISTEN.service, { user = 'test' })
cn ~= nil
cn:close()
cn = remote.connect(LISTEN.host, LISTEN.service, { password = 'test' })
cn ~= nil
cn:close()

-- #544 usage for remote[point]method
cn = remote.connect(LISTEN.host, LISTEN.service)

cn:eval('return true')
cn.eval('return true')

cn.ping()

cn:close()

remote.self:eval('return true')
remote.self.eval('return true')


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

box.schema.user.revoke('netbox', 'read, write, execute', 'universe');
box.schema.user.drop('netbox')

-- #594: bad argument #1 to 'setmetatable' (table expected, got number)
test_run:cmd("setopt delimiter ';'")
function gh594()
    local cn = remote.connect(box.cfg.listen)
    local ping = fiber.create(function() cn:ping() end)
    cn:call('dostring', {'return 2 + 2'})
    cn:close()
end;
test_run:cmd("setopt delimiter ''");
gh594()

-- #636: Reload schema on demand
sp = box.schema.space.create('test_old')
_ = sp:create_index('primary')
sp:insert{1, 2, 3}

con = remote.new(box.cfg.listen)
con:ping()
con.space.test_old:select{}
con.space.test == nil

sp = box.schema.space.create('test')
_ = sp:create_index('primary')
sp:insert{2, 3, 4}

con.space.test == nil
con:reload_schema()
con.space.test:select{}

box.space.test:drop()
box.space.test_old:drop()
con:close()

name = string.match(arg[0], "([^,]+)%.lua")
file_log = require('fio').open(name .. '.log', {'O_RDONLY', 'O_NONBLOCK'})
file_log:seek(0, 'SEEK_END') ~= 0

test_run:cmd("setopt delimiter ';'")

_ = fiber.create(
   function()
         local conn = require('net.box').new(box.cfg.listen)
         conn:call('no_such_function', {})
         conn:close()
   end
);
test_run:cmd("setopt delimiter ''");
test_run:grep_log("default", "ER_NO_SUCH_PROC")

-- gh-983 selecting a lot of data crashes the server or hangs the
-- connection

-- gh-983 test case: iproto connection selecting a lot of data
_ = box.schema.space.create('test', { temporary = true })
_ = box.space.test:create_index('primary', {type = 'TREE', parts = {1,'unsigned'}})

data1k = "aaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhhaaaabbbbccccddddeeeeffffgggghhhh"

for i = 0,10000 do box.space.test:insert{i, data1k} end

net = require('net.box')
c = net:connect(box.cfg.listen)
r = c.space.test:select(nil, {limit=5000})
box.space.test:drop()

-- gh-970 gh-971 UPSERT over network
_ = box.schema.space.create('test')
_ = box.space.test:create_index('primary', {type = 'TREE', parts = {1,'unsigned'}})
_ = box.space.test:create_index('covering', {type = 'TREE', parts = {1,'unsigned',3,'string',2,'unsigned'}})
_ = box.space.test:insert{1, 2, "string"}
c = net:connect(box.cfg.listen)
c.space.test:select{}
c.space.test:upsert({1, 2, 'nothing'}, {{'+', 2, 1}}) -- common update
c.space.test:select{}
c.space.test:upsert({2, 4, 'something'}, {{'+', 2, 1}}) -- insert
c.space.test:select{}
c.space.test:upsert({2, 4, 'nothing'}, {{'+', 3, 100500}}) -- wrong operation
c.space.test:select{}

-- gh-1729 net.box index metadata incompatible with local metadata
c.space.test.index.primary.parts
c.space.test.index.covering.parts

box.space.test:drop()

-- CALL vs CALL_16 in connect options
function echo(...) return ... end
c = net.connect(box.cfg.listen)
c:call('echo', {42})
c:eval('return echo(...)', {42})
-- invalid arguments
c:call('echo', 42)
c:eval('return echo(...)', 42)
c:close()
c = net.connect(box.cfg.listen, {call_16 = true})
c:call('echo', 42)
c:eval('return echo(...)', 42)
c:close()

--
-- gh-2195 export pure msgpack from net.box
--

space = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
c = net.connect(box.cfg.listen)
ibuf = require('buffer').ibuf()

c:ping()
c.space.test ~= nil

c.space.test:replace({1, 'hello'})

-- replace
c.space.test:replace({2}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- insert
c.space.test:insert({3}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- update
c.space.test:update({3}, {}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result
c.space.test.index.primary:update({3}, {}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- upsert
c.space.test:upsert({4}, {}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- delete
c.space.test:upsert({4}, {}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- select
c.space.test.index.primary:select({3}, {iterator = 'LE', buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- select
len = c.space.test:select({}, {buffer = ibuf})
ibuf.rpos + len == ibuf.wpos
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
ibuf.rpos == ibuf.wpos
len
result

-- call
c:call("echo", {1, 2, 3}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result
c:call("echo", {}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result
c:call("echo", nil, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- eval
c:eval("echo(...)", {1, 2, 3}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result
c:eval("echo(...)", {}, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result
c:eval("echo(...)", nil, {buffer = ibuf})
ibuf.rpos, result = msgpack.ibuf_decode(ibuf.rpos)
result

-- unsupported methods
c.space.test:get({1}, { buffer = ibuf})
c.space.test.index.primary:min({}, { buffer = ibuf})
c.space.test.index.primary:max({}, { buffer = ibuf})
c.space.test.index.primary:count({}, { buffer = ibuf})
c.space.test.index.primary:get({1}, { buffer = ibuf})

-- error handling
rpos, wpos = ibuf.rpos, ibuf.wpos
c.space.test:insert({1}, {buffer = ibuf})
ibuf.rpos == rpos, ibuf.wpos == wpos

ibuf = nil
c:close()
space:drop()

-- gh-1904 net.box hangs in :close() if a fiber was cancelled
-- while blocked in :_wait_state() in :_request()
options = {user = 'netbox', password = 'badpass', wait_connected = false, reconnect_after = 0.01}
c = net:new(box.cfg.listen, options)
f = fiber.create(function() c:call("") end)
fiber.sleep(0.01)
f:cancel(); c:close()

-- check for on_schema_reload callback
test_run:cmd("setopt delimiter ';'")
do
    local a = 0
    function osr_cb()
        a = a + 1
    end
    local con = net.new(box.cfg.listen, {
        wait_connected = false
    })
    con:on_schema_reload(osr_cb)
    con:wait_connected()
    con.space._schema:select{}
    box.schema.space.create('misisipi')
    box.space.misisipi:drop()
    con.space._schema:select{}
    con:close()
    con = nil

    return a
end;
do
    local a = 0
    function osr_cb()
        a = a + 1
    end
    local con = net.new(box.cfg.listen, {
        wait_connected = true
    })
    con:on_schema_reload(osr_cb)
    con.space._schema:select{}
    box.schema.space.create('misisipi')
    box.space.misisipi:drop()
    con.space._schema:select{}
    con:close()
    con = nil

    return a
end;
test_run:cmd("setopt delimiter ''");

box.schema.user.revoke('guest', 'read,write,execute', 'universe')

-- Tarantool < 1.7.1 compatibility (gh-1533)
c = net.new(box.cfg.listen)
c:ping()
c:close()

-- Test for connect_timeout > 0 in netbox connect
test_run:cmd("setopt delimiter ';'");
greeting =
"Tarantool 1.7.3 (Lua console)~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n" ..
"type 'help' for interactive help~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n";
socket = require('socket');
srv = socket.tcp_server('localhost', 3392, {
    handler = function(fd)
        local fiber = require('fiber')
        fiber.sleep(0.1)
        fd:write(greeting)
    end
});
-- we must get timeout
nb = net.new('localhost:3392', {
    wait_connected = true, console = true,
    connect_timeout = 0.01
});
nb.error == "Timeout exceeded" or nb.error == "Connection timed out";
nb:close();
-- we must get peer closed
nb = net.new('localhost:3392', {
    wait_connected = true, console = true,
    connect_timeout = 0.2
});
nb.error ~= "Timeout exceeded";
nb:close();
test_run:cmd("setopt delimiter ''");
srv:close()

test_run:cmd("clear filter")

--
-- gh-2402 net.box doesn't support space:format()
--

space = box.schema.space.create('test', {format={{name="id", type="unsigned"}}})
space ~= nil
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest','read,write,execute','space', 'test')

c = net.connect(box.cfg.listen)

c:ping()
c.space.test ~= nil

format = c.space.test:format()

format[1] ~= nil
format[1].name == "id"
format[1].type == "unsigned"

c.space.test:format({})

c:close()
space:drop()

--
-- Check that it's possible to get connection object form net.box space
--

space = box.schema.space.create('test', {format={{name="id", type="unsigned"}}})
space ~= nil
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest','read,write,execute','space', 'test')

c = net.connect(box.cfg.listen)

c:ping()
c.space.test ~= nil

c.space.test.connection == c
box.schema.user.revoke('guest','read,write,execute','space', 'test')
c:close()

--
-- gh-2642: box.session.type()
--

box.schema.user.grant('guest','read,write,execute','universe')
c = net.connect(box.cfg.listen)
c:call("box.session.type")
c:close()

--
-- On_connect/disconnect triggers.
--
test_run:cmd('create server connecter with script = "box/proxy.lua"')
test_run:cmd('start server connecter')
test_run:cmd("set variable connect_to to 'connecter.listen'")
conn = net.connect(connect_to, { reconnect_after = 0.1 })
conn.state
connected_cnt = 0
disconnected_cnt = 0
function on_connect() connected_cnt = connected_cnt + 1 end
function on_disconnect() disconnected_cnt = disconnected_cnt + 1 end
conn:on_connect(on_connect)
conn:on_disconnect(on_disconnect)
test_run:cmd('stop server connecter')
test_run:cmd('start server connecter')
while conn.state ~= 'active' do fiber.sleep(0.1) end
connected_cnt
disconnected_cnt
conn:close()
disconnected_cnt
test_run:cmd('stop server connecter')
test_run:cmd('cleanup server connecter')

--
-- gh-2401 update pseudo objects not replace them
--
space:drop()
space = box.schema.space.create('test')
c = net.connect(box.cfg.listen)
cspace = c.space.test
space.index.test_index == nil
cspace.index.test_index == nil
_ = space:create_index("test_index", {parts={1, 'string'}})
c:reload_schema()
space.index.test_index ~= nil
cspace.index.test_index ~= nil
c.space.test.index.test_index ~= nil

-- cleanup
box.schema.user.revoke('guest','read,write,execute','universe')

space:drop()
