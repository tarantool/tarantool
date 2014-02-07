space = box.schema.create_space('tweedledum')
space:create_index('primary', { type = 'hash'})
remote = box.net.box.new('localhost', box.cfg.primary_port, '0.5')
type(remote)
remote:ping()
remote:ping()
box.net.box.ping(remote)
space:insert{123, 'test1', 'test2'}
space:select{123}
tuple = remote:select(space.n, 123)

function test(...) return box.tuple.new({ 123, 456 }) end
f, a = box.call_loadproc('test')
type(f)
type(a)

unpack(remote:call('test'))
function test(...) return box.tuple.new({ ... }) end
unpack(remote:call('test', 123, 345, { 678, 910 }))
function test(...) return box.tuple.new({ ... }), box.tuple.new({ ... }) end
unpack(remote:call('test', 123, 345, { 678, 910 }))
test = { a = 'a', b = function(self, ...) return box.tuple.new(123) end }
unpack(remote:call('test:b'))
test.b = function(self, ...) return box.tuple.new({self.a, ...}) end
f, a = box.call_loadproc('test:b')
type(f)
type(a)
a.a
f, a = box.call_loadproc('test.b')
type(f)
type(a)

unpack(remote:call('test:b'))
unpack(remote:call('test:b', 'b', 'c'))
unpack(remote:call('test:b', 'b', 'c', 'd', 'e'))
unpack(remote:call('test:b', 'b', { 'c', { d = 'e' } }))


test = { a = { c = 1, b = function(self, ...) return { self.c, ... } end } }
f, a = box.call_loadproc('test.a:b')
type(f)
type(a)
a.c
f, a = box.call_loadproc('test.a.b')
type(f)
type(a)

unpack(remote:call('test.a:b', 123))



box.space.tweedledum:select(123)
box.space.tweedledum:select({123})
unpack(remote:call('box.space.tweedledum:select', 123))
unpack(remote:call('box.space.tweedledum:select', {123}))

slf, foo = box.call_loadproc('box.net.self:select')
type(slf)
type(foo)

tuple
type(tuple)
#tuple

space:update(123, {{'=', 1, 'test1-updated'}})
remote:update(space.n, 123, {{'=', 2, 'test2-updated'}})

space:insert{123, 'test1', 'test2'}
remote:insert(space.n, {123, 'test1', 'test2'})

remote:insert(space.n, {345, 'test1', 'test2'})
remote:select(space.n, {345})
unpack(remote:call('box.space.tweedledum:select', 345))
space:select{345}

remote:replace(space.n, {345, 'test1-replaced', 'test2-replaced'})
space:select{345}

space:eselect({}, { iterator = 'GE', limit = 1000 })
box.net.self:eselect(space.n, 0, {}, { iterator = 'GE', limit = 1000 })
remote:eselect(space.n, 0, {}, { limit = 1000, iterator = 'GE' })
space:select{345}
remote:select(space.n, {345})
remote:timeout(0.5):select(space.n, {345})



box.net.self:insert(space.n, {12345, 'test1', 'test2'})
box.net.self:replace(space.n, {12346, 'test1', 'test2'})
box.net.self:update(space.n, 12345, {{ '=', 1, 'test11' }})
box.net.self:update(space.n, 12347, {{ '=', 1, 'test11' }})
box.net.self:delete(space.n, 12346)


unpack(remote:call('box.fiber.sleep', .01))
unpack(remote:timeout(0.01):call('box.fiber.sleep', 10))

--# setopt delimiter ';'
pstart = box.time();
parallel = {};
function parallel_foo(id)
    box.fiber.sleep(math.random() * .05)
    return id
end;
parallel_foo('abc');
for i = 1, 20 do
    box.fiber.resume(
        box.fiber.create(
            function()
                box.fiber.detach()
                local s = string.format('%07d', i)
                local so = remote:call('parallel_foo', s)
                table.insert(parallel, s == so[1][0])
            end
        )
    )
end;
for i = 1, 20 do
    if #parallel == 20 then
        break
    end
    box.fiber.sleep(0.1)
end;
--# setopt delimiter ''
unpack(parallel)
#parallel
box.time() - pstart < 0.5



box.net.self.rpc.box.space.tweedledum.index.primary:select(12345)
remote.rpc.box.space.tweedledum.index.primary:eselect(12345)
remote.rpc.box.space.tweedledum.index.primary:select(12345)

remote:close()
remote:close()
remote:ping()

space:drop()
