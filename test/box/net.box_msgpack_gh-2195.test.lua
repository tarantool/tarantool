msgpack = require 'msgpack'
test_run = require('test_run').new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")
net = require('net.box')

-- CALL vs CALL_16 in connect options
function echo(...) return ... end
box.schema.user.grant('guest', 'execute', 'universe')
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
box.schema.user.revoke('guest', 'execute', 'universe')

--
-- gh-2195 export pure msgpack from net.box
--

space = box.schema.space.create('test')
_ = box.space.test:create_index('primary')
box.schema.user.grant('guest', 'read,write', 'space', 'test')
box.schema.user.grant('guest', 'execute', 'universe')
c = net.connect(box.cfg.listen)
ibuf = require('buffer').ibuf()

c:ping()
c.space.test ~= nil

c.space.test:replace({1, 'hello'})

-- replace
c.space.test:replace({2}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- replace + skip_header
c.space.test:replace({2}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- insert
c.space.test:insert({3}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- insert + skip_header
_ = space:delete({3})
c.space.test:insert({3}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- update
c.space.test:update({3}, {}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c.space.test.index.primary:update({3}, {}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- update + skip_header
c.space.test:update({3}, {}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c.space.test.index.primary:update({3}, {}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- upsert
c.space.test:upsert({4}, {}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- upsert + skip_header
c.space.test:upsert({4}, {}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- delete
c.space.test:upsert({4}, {}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- delete + skip_header
c.space.test:upsert({4}, {}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- select
c.space.test.index.primary:select({3}, {iterator = 'LE', buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- select + skip_header
c.space.test.index.primary:select({3}, {iterator = 'LE', buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- select
len = c.space.test:select({}, {buffer = ibuf})
ibuf.rpos + len == ibuf.wpos
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
ibuf.rpos == ibuf.wpos
len
result

-- select + skip_header
len = c.space.test:select({}, {buffer = ibuf, skip_header = true})
ibuf.rpos + len == ibuf.wpos
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
ibuf.rpos == ibuf.wpos
len
result

-- call
c:call("echo", {1, 2, 3}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:call("echo", {}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:call("echo", nil, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- call + skip_header
c:call("echo", {1, 2, 3}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:call("echo", {}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:call("echo", nil, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- eval
c:eval("echo(...)", {1, 2, 3}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:eval("echo(...)", {}, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:eval("echo(...)", nil, {buffer = ibuf})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- eval + skip_header
c:eval("echo(...)", {1, 2, 3}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:eval("echo(...)", {}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
c:eval("echo(...)", nil, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result

-- make several request into a buffer with skip_header, then read
-- results
c:call("echo", {1, 2, 3}, {buffer = ibuf, skip_header = true})
c:call("echo", {1, 2, 3}, {buffer = ibuf, skip_header = true})
c:call("echo", {1, 2, 3}, {buffer = ibuf, skip_header = true})
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
result
result, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos)
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
box.schema.user.revoke('guest', 'execute', 'universe')
