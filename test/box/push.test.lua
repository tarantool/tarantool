test_run = require('test_run').new()
--
-- gh-2677: box.session.push binary protocol tests.
--

--
-- Usage.
-- gh-4689: sync is deprecated, accepts only 'data' parameter.
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
    box.session.push('started dml')
    s:replace{key}
    box.session.push('continued dml')
    s:replace{-key}
    box.session.push('finished dml')
    return key
end;
function do_pushes(val)
    for i = 1, 5 do
        box.session.push(i)
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
    for i = 1, 5 do
        box.session.push(i + 100)
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

--
-- gh-3859: box.session.push() succeeds even after the connection
-- is closed.
--
chan_func = fiber.channel()
chan_push = fiber.channel()
chan_disconnected = fiber.channel()
on_disconnect = box.session.on_disconnect(function() chan_disconnected:put(true) end)
test_run:cmd("setopt delimiter ';'")
function do_long_and_push()
    chan_func:put(true)
    chan_push:get()
    ok, err = box.session.push(100)
    chan_push:put(err)
end;
test_run:cmd("setopt delimiter ''");
box.schema.func.create('do_long_and_push')
box.schema.user.grant('guest', 'execute', 'function', 'do_long_and_push')
f = fiber.create(function() c:call('do_long_and_push') end)
chan_func:get()
c:close()
chan_disconnected:get()
chan_push:put(true)
chan_push:get()
box.schema.func.drop('do_long_and_push')
box.session.on_disconnect(nil, on_disconnect)

--
-- gh-4734: C API for session push.
--
build_path = os.getenv("BUILDDIR")
old_cpath = package.cpath
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..old_cpath

box.schema.func.create('function1.test_push', {language = 'C'})
box.schema.user.grant('guest', 'super')
c = netbox.connect(box.cfg.listen)
messages = {}
c:call('function1.test_push',   \
       {1, 2, 3},               \
       {on_push = table.insert, \
        on_push_ctx = messages})
messages
c:close()

--
-- C can push to the console.
--

-- A string having 0 byte inside. Check that it is handled fine.
s = '\x41\x00\x43'

console = require('console')
fio = require('fio')
socket = require('socket')
sock_path = fio.pathjoin(fio.cwd(), 'console.sock')
_ = fio.unlink(sock_path)
server = console.listen(sock_path)
client = socket.tcp_connect('unix/', sock_path)
_ = client:read({chunk = 128})
_ = client:write("box.func['function1.test_push']:call({1, 2, 3, s})\n")
client:read("\n...\n")
_ = client:read("\n...\n")
-- Lua output format is supported too.
_ = client:write("\\set output lua\n")
_ = client:read(";")
_ = client:write("box.func['function1.test_push']:call({1, 2, 3, s})\n")
client:read(";")
_ = client:read(";")
client:close()
server:close()

box.schema.user.revoke('guest', 'super')
box.schema.func.drop('function1.test_push')

package.cpath = old_cpath
