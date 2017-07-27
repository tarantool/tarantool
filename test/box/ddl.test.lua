env = require('test_run')
test_run = env.new()

fiber = require'fiber'

-- simple test for parallel ddl execution
_ = box.schema.space.create('test'):create_index('pk')

ch = fiber.channel(2)

test_run:cmd("setopt delimiter ';'")

function f1()
    box.space.test:create_index('sec', {parts = {2, 'num'}})
    ch:put(true)
end;

function f2()
    box.space.test:create_index('third', {parts = {3, 'string'}})
    ch:put(true)
end;

test_run:cmd("setopt delimiter ''");

_ = {fiber.create(f1), fiber.create(f2)}

ch:get()
ch:get()

_ = box.space.test:drop()

test_run:cmd('restart server default')

env = require('test_run')
test_run = env.new()
fiber = require'fiber'

ch = fiber.channel(2)

--issue #928
space = box.schema.space.create('test_trunc')
_ = space:create_index('pk')
_ = box.space.test_trunc:create_index('i1', {type = 'hash', parts = {2, 'STR'}})
_ = box.space.test_trunc:create_index('i2', {type = 'hash', parts = {2, 'STR'}})

function test_trunc() space:truncate() ch:put(true) end

_ = {fiber.create(test_trunc), fiber.create(test_trunc)}
_ = {ch:get(), ch:get()}
space:drop()

-- index should not crash after alter
space = box.schema.space.create('test_swap')
index = space:create_index('pk')
space:replace({1, 2, 3})
index:rename('primary')
index2 = space:create_index('sec')
space:replace({2, 3, 1})
space:select()
space:drop()


ch = fiber.channel(3)

_ = box.schema.space.create('test'):create_index('pk')

test_run:cmd("setopt delimiter ';'")
function add_index()
    box.space.test:create_index('sec', {parts = {2, 'num'}})
    ch:put(true)
end;

function insert_tuple(tuple)
    ch:put({pcall(box.space.test.replace, box.space.test, tuple)})
end;
test_run:cmd("setopt delimiter ''");

_ = {fiber.create(insert_tuple, {1, 2, 'a'}), fiber.create(add_index), fiber.create(insert_tuple, {2, '3', 'b'})}
{ch:get(), ch:get(), ch:get()}

box.space.test:select()

test_run:cmd('restart server default')

box.space.test:select()
box.space.test:drop()

-- gh-2336 crash if format called twice during snapshot
fiber = require'fiber'

space = box.schema.space.create('test_format')
_ = space:create_index('pk', { parts = { 1,'str' }})
space:format({{ name ="key"; type = "string" }, { name ="dataAB"; type = "string" }})
str = string.rep("t",1024)
for i = 1, 10000 do space:insert{tostring(i), str} end
ch = fiber.channel(3)
_ = fiber.create(function() fiber.yield() box.snapshot() ch:put(true) end)
format = {{name ="key"; type = "string"}, {name ="data"; type = "string"}}
for i = 1, 2 do fiber.create(function() fiber.yield() space:format(format) ch:put(true) end) end

{ch:get(), ch:get(), ch:get()}

space:drop()
