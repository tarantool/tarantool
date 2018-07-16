test_run = require('test_run').new()
--
-- gh-2677: box.session.push binary protocol tests.
--

--
-- Usage.
--
box.session.push()
box.session.push(1, 'a')

fiber = require('fiber')
messages = {}
test_run:cmd("setopt delimiter ';'")
-- Do push() with no explicit sync. Use session.sync() by default.
function do_pushes()
    for i = 1, 5 do
        box.session.push(i)
        fiber.sleep(0.01)
    end
    return 300
end;
test_run:cmd("setopt delimiter ''");

netbox = require('net.box')

box.schema.func.create('do_pushes')
box.schema.user.grant('guest', 'execute', 'function', 'do_pushes')

c = netbox.connect(box.cfg.listen)
c:ping()
c:call('do_pushes', {}, {on_push = table.insert, on_push_ctx = messages})
messages

-- Add a little stress: many pushes with different syncs, from
-- different fibers and DML/DQL requests.

catchers = {}
started = 0
finished = 0
s = box.schema.create_space('test', {format = {{'field1', 'integer'}}})
box.schema.user.grant('guest', 'write', 'space', 'test')
pk = s:create_index('pk')
c:reload_schema()
test_run:cmd("setopt delimiter ';'")
function dml_push_and_dml(key)
    local sync = box.session.sync()
    box.session.push('started dml', sync)
    s:replace{key}
    box.session.push('continued dml', sync)
    s:replace{-key}
    box.session.push('finished dml', sync)
    return key
end;
function do_pushes(val)
    local sync = box.session.sync()
    for i = 1, 5 do
        box.session.push(i, sync)
        fiber.yield()
    end
    return val
end;
function push_catcher_f()
    fiber.yield()
    started = started + 1
    local catcher = {messages = {}, retval = nil, is_dml = false}
    catcher.retval = c:call('do_pushes', {started},
                            {on_push = table.insert,
                             on_push_ctx = catcher.messages})
    table.insert(catchers, catcher)
    finished = finished + 1
end;
function dml_push_and_dml_f()
    fiber.yield()
    started = started + 1
    local catcher = {messages = {}, retval = nil, is_dml = true}
    catcher.retval = c:call('dml_push_and_dml', {started},
                            {on_push = table.insert,
                             on_push_ctx = catcher.messages})
    table.insert(catchers, catcher)
    finished = finished + 1
end;
box.schema.func.create('dml_push_and_dml');
box.schema.user.grant('guest', 'execute', 'function', 'dml_push_and_dml');

-- At first check that a pushed message can be ignored in a binary
-- protocol too.
c:call('do_pushes', {300});
-- Then do stress.
for i = 1, 200 do
    fiber.create(dml_push_and_dml_f)
    fiber.create(push_catcher_f)
end;
while finished ~= 400 do fiber.sleep(0.1) end;

box.schema.func.drop('dml_push_and_dml')

failed_catchers = {};

for _, c in pairs(catchers) do
    if c.is_dml then
        if #c.messages ~= 3 or c.messages[1] ~= 'started dml' or
           c.messages[2] ~= 'continued dml' or
           c.messages[3] ~= 'finished dml' or s:get{c.retval} == nil or
           s:get{-c.retval} == nil then
            table.insert(failed_catchers, c)
        end
    else
        if c.retval == nil or #c.messages ~= 5 then
            table.insert(failed_catchers, c)
        else
            for k, v in pairs(c.messages) do
                if k ~= v then
                    table.insert(failed_catchers, c)
                    break
                end
            end
        end
    end
end;
test_run:cmd("setopt delimiter ''");

failed_catchers

#s:select{}

--
-- Ok to push NULL.
--
function push_null() box.session.push(box.NULL) end
messages = {}
box.schema.func.create('push_null')
box.schema.user.grant('guest', 'execute', 'function', 'push_null')
c:call('push_null', {}, {on_push = table.insert, on_push_ctx = messages})
messages
box.schema.func.drop('push_null')
--
-- Test binary pushes.
--
ibuf = require('buffer').ibuf()
msgpack = require('msgpack')
messages = {}
resp_len = c:call('do_pushes', {300}, {on_push = table.insert, on_push_ctx = messages, buffer = ibuf})
resp_len
messages
decoded = {}
r = nil
for i = 1, #messages do r, ibuf.rpos = msgpack.decode_unchecked(ibuf.rpos) table.insert(decoded, r) end
decoded
r, _ = msgpack.decode_unchecked(ibuf.rpos)
r

--
-- Test error in __serialize.
--
ok = nil
err = nil
messages = {}
t = setmetatable({100}, {__serialize = function() error('err in ser') end})
function do_push() ok, err = box.session.push(t) end
box.schema.func.create('do_push')
box.schema.user.grant("guest", "execute", "function", "do_push")
c:call('do_push', {}, {on_push = table.insert, on_push_ctx = messages})
ok, err
messages
box.schema.func.drop('do_push')
--
-- Test push from a non-call request.
--
s:truncate()
_ = s:on_replace(function() box.session.push('replace') end)
c:reload_schema()
c.space.test:replace({200}, {on_push = table.insert, on_push_ctx = messages})
messages
s:select{}

c:close()
s:drop()

--
-- Ensure can not push in background.
--
f = fiber.create(function() ok, err = box.session.push(100) end)
while f:status() ~= 'dead' do fiber.sleep(0.01) end
ok, err

--
-- Async iterable pushes.
--
c = netbox.connect(box.cfg.listen)
cond = fiber.cond()
test_run:cmd("setopt delimiter ';'")
function do_pushes()
    local sync = box.session.sync()
    for i = 1, 5 do
        box.session.push(i + 100, sync)
        cond:wait()
    end
    return true
end;
test_run:cmd("setopt delimiter ''");

-- Can not combine callback and async mode.
ok, err = pcall(c.call, c, 'do_pushes', {}, {is_async = true, on_push = function() end})
ok
err:find('use future:pairs()') ~= nil
future = c:call('do_pushes', {}, {is_async = true})
-- Try to ignore pushes.
while not future:wait_result(0.01) do cond:signal() end
future:result()

-- Even if pushes are ignored, they still are available via pairs.
messages = {}
keys = {}
for i, message in future:pairs() do table.insert(messages, message) table.insert(keys, i) end
messages
keys

-- Test error.
s = box.schema.create_space('test')
pk = s:create_index('pk')
s:replace{1}

box.schema.user.grant('guest', 'write', 'space', 'test')

function do_push_and_duplicate() box.session.push(100) s:insert{1} end
box.schema.func.create('do_push_and_duplicate')
box.schema.user.grant('guest', 'execute', 'function', 'do_push_and_duplicate')
future = c:call('do_push_and_duplicate', {}, {is_async = true})
future:wait_result(1000)
messages = {}
keys = {}
for i, message in future:pairs() do table.insert(messages, message) table.insert(keys, i) end
messages
keys

box.schema.func.drop('do_push_and_duplicate')
box.schema.func.drop('do_pushes')
s:drop()
c:close()
