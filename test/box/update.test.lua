s = box.schema.space.create('tweedledum')
index = s:create_index('pk')

-- test delete field
s:insert{1000001, 1000002, 1000003, 1000004, 1000005}
s:update({1000001}, {{'#', 1, 1}})
s:truncate()

-- test arithmetic
s:insert{1, 0}
s:update(1, {{'+', 2, 10}})
s:update(1, {{'+', 2, 15}})
s:update(1, {{'-', 2, 5}})
s:update(1, {{'-', 2, 20}})
s:update(1, {{'|', 2, 0x9}})
s:update(1, {{'|', 2, 0x6}})
s:update(1, {{'&', 2, 0xabcde}})
s:update(1, {{'&', 2, 0x2}})
s:update(1, {{'^', 2, 0xa2}})
s:update(1, {{'^', 2, 0xa2}})
s:truncate()

-- test delete multiple fields
s:insert{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
s:update({0}, {{'#', 42, 1}})
s:update({0}, {{'#', 4, 'abirvalg'}})
s:update({0}, {{'#', 2, 1}, {'#', 4, 2}, {'#', 6, 1}})
s:update({0}, {{'#', 4, 3}})
s:update({0}, {{'#', 5, 123456}})
s:update({0}, {{'#', 3, 4294967295}})
s:update({0}, {{'#', 2, 0}})
s:truncate()

-- test insert field
s:insert{1, 3, 6, 9}
s:update({1}, {{'!', 2, 2}})
s:update({1}, {{'!', 4, 4}, {'!', 4, 5}, {'!', 5, 7}, {'!', 5, 8}})
s:update({1}, {{'!', 10, 10}, {'!', 10, 11}, {'!', 10, 12}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'#', 2, 1}, {'!', 2, 'inserted tuple'}, {'=', 3, 'set tuple'}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'=', 2, 'set tuple'}, {'!', 2, 'inserted tuple'}, {'#', 3, 1}})
s:update({1}, {{'!', 1, 3}, {'!', 1, 2}})
s:truncate()

-- test update's assign opearations
s:replace{1, 'field string value'}
s:update({1}, {{'=', 2, 'new field string value'}, {'=', 3, 42}, {'=', 4, 0xdeadbeef}})

-- test multiple update opearations on the same field
s:update({1}, {{'+', 3, 16}, {'&', 4, 0xffff0000}, {'|', 4, 0x0000a0a0}, {'^', 4, 0xffff00aa}})

-- test update splice operation
s:replace{1953719668, 'something to splice'}
s:update(1953719668, {{':', 2, 1, 4, 'no'}})
s:update(1953719668, {{':', 2, 1, 2, 'every'}})
-- check an incorrect offset
s:update(1953719668, {{':', 2, 100, 2, 'every'}})
s:update(1953719668, {{':', 2, -100, 2, 'every'}})
s:truncate()
s:insert{1953719668, 'hello', 'october', '20th'}:unpack()
s:truncate()
s:insert{1953719668, 'hello world'}
s:update(1953719668, {{'=', 2, 'bye, world'}})
s:delete{1953719668}

s:replace({10, 'abcde'})
s:update(10,  {{':', 2, 0, 0, '!'}})
s:update(10,  {{':', 2, 1, 0, '('}})
s:update(10,  {{':', 2, 2, 0, '({'}})
s:update(10,  {{':', 2, -1, 0, ')'}})
s:update(10,  {{':', 2, -2, 0, '})'}})

-- test update delete operations
s:update({1}, {{'#', 4, 1}, {'#', 3, 1}})

-- test update insert operations
s:update({1}, {{'!', 2, 1}, {'!', 2, 2}, {'!', 2, 3}, {'!', 2, 4}})

-- s:update: zero field
s:insert{48}
s:update(48, {{'=', 0, 'hello'}})

-- s:update: push/pop fields
s:insert{1684234849}
s:update({1684234849}, {{'#', 2, 1}})
s:update({1684234849}, {{'!', -1, 'push1'}})
s:update({1684234849}, {{'!', -1, 'push2'}})
s:update({1684234849}, {{'!', -1, 'push3'}})
s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap1'}})
s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap2'}})
s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap3'}})
s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop1'}})
s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop2'}})
s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop3'}})

--
-- negative indexes
--

box.tuple.new({1, 2, 3, 4, 5}):update({{'!', 0, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -1, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -3, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -5, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -6, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -7, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -100500, 'Test'}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'=', 0, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -1, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -3, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -5, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -6, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -100500, 'Test'}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'+', 0, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -1, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -3, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -5, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -6, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -100500, 100}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'|', 0, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -1, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -3, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -5, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -6, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -100500, 100}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'#', 0, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -1, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -3, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -5, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -6, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -100500, 1}})

--
-- #416: UPDATEs from Lua can't be properly restored due to one based indexing
--
env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')

s = box.space.tweedledum
s:select{}
s:truncate()
s:drop()

-- #521: Cryptic error message in update operation
s = box.schema.space.create('tweedledum')
index = s:create_index('pk')
s:insert{1, 2, 3}
s:update({1})
s:update({1}, {'=', 1, 1})
s:drop()

-- #528: Different types in arithmetical update, overflow check
ffi = require('ffi')
s = box.schema.create_space('tweedledum')
index = s:create_index('pk')
s:insert{0, -1}
-- + --
s:update({0}, {{'+', 2, "a"}}) -- err
s:update({0}, {{'+', 2, 10}}) -- neg(ative) + pos(itive) = pos(itive) 9
s:update({0}, {{'+', 2, 5}}) -- pos + pos = pos 14
s:update({0}, {{'+', 2, -4}}) -- pos + neg = pos 10
s:update({0}, {{'+', 2, -22}}) -- pos + neg = neg -12
s:update({0}, {{'+', 2, -3}}) -- neg + neg = neg -15
s:update({0}, {{'+', 2, 7}}) -- neg + pos = neg -8
-- - --
s:update({0}, {{'-', 2, "a"}}) -- err
s:update({0}, {{'-', 2, 16}}) -- neg(ative) - pos(itive) = neg(ative) -24
s:update({0}, {{'-', 2, -4}}) -- neg - neg = neg 20
s:update({0}, {{'-', 2, -32}}) -- neg - neg = pos 12
s:update({0}, {{'-', 2, 3}}) -- pos - pos = pos 9
s:update({0}, {{'-', 2, -5}}) -- pos - neg = pos 14
s:update({0}, {{'-', 2, 17}}) -- pos - pos = neg -3
-- bit --
s:replace{0, 0} -- 0
s:update({0}, {{'|', 2, 24}}) -- 24
s:update({0}, {{'|', 2, 2}}) -- 26
s:update({0}, {{'&', 2, 50}}) -- 18
s:update({0}, {{'^', 2, 6}}) -- 20
s:update({0}, {{'|', 2, -1}}) -- err
s:update({0}, {{'&', 2, -1}}) -- err
s:update({0}, {{'^', 2, -1}}) -- err
s:replace{0, -1} -- -1
s:update({0}, {{'|', 2, 2}}) -- err
s:update({0}, {{'&', 2, 40}}) -- err
s:update({0}, {{'^', 2, 6}}) -- err
s:replace{0, 1.5} -- 1.5
s:update({0}, {{'|', 2, 2}}) -- err
s:update({0}, {{'&', 2, 40}}) -- err
s:update({0}, {{'^', 2, 6}}) -- err
-- double
s:replace{0, 5} -- 5
s:update({0}, {{'+', 2, 1.5}}) -- int + double = double 6.5
s:update({0}, {{'|', 2, 2}}) -- err (double!)
s:update({0}, {{'-', 2, 0.5}}) -- double - double = double 6
s:update({0}, {{'+', 2, 1.5}}) -- double + double = double 7.5
-- float
s:replace{0, ffi.new("float", 1.5)} -- 1.5
s:update({0}, {{'+', 2, 2}}) -- float + int = float 3.5
s:update({0}, {{'+', 2, ffi.new("float", 3.5)}}) -- float + int = float 7
s:update({0}, {{'|', 2, 2}}) -- err (float!)
s:update({0}, {{'-', 2, ffi.new("float", 1.5)}}) -- float - float = float 5.5
s:update({0}, {{'+', 2, ffi.new("float", 3.5)}}) -- float + float = float 9
s:update({0}, {{'-', 2, ffi.new("float", 9)}}) -- float + float = float 0
s:update({0}, {{'+', 2, ffi.new("float", 1.2)}}) -- float + float = float 1.2
-- overflow --
s:replace{0, 0xfffffffffffffffeull}
s:update({0}, {{'+', 2, 1}}) -- ok
s:update({0}, {{'+', 2, 1}}) -- overflow
s:update({0}, {{'+', 2, 100500}}) -- overflow
s:replace{0, 1}
s:update({0}, {{'+', 2, 0xffffffffffffffffull}})  -- overflow
s:replace{0, -1}
s:update({0}, {{'+', 2, 0xffffffffffffffffull}})  -- ok
s:replace{0, 0}
s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})  -- ok
s:replace{0, -1}
s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})  -- ok
s:replace{0, -2}
s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})  -- overflow
s:replace{0, 1}
s:update({0}, {{'-', 2, 0xffffffffffffffffull}})  -- overflow
s:replace{0, 0xffffffffffffffefull}
s:update({0}, {{'-', 2, -16}})  -- ok
s:update({0}, {{'-', 2, -16}})  -- overflow
s:replace{0, -0x4000000000000000ll}
s:update({0}, {{'+', 2, -0x4000000000000000ll}})  -- ok
s:replace{0, -0x4000000000000000ll}
s:update({0}, {{'+', 2, -0x4000000000000001ll}})  -- overflow

-- some wrong updates --
s:update({0}, 0)
s:update({0}, {})
s:update({0}, {'+', 2, 2})
s:update({0}, {{}})
s:update({0}, {{'+'}})
s:update({0}, {{'+', 0}})
s:update({0}, {{'+', '+', '+'}})
s:update({0}, {{0, 0, 0}})

-- test for https://github.com/tarantool/tarantool/issues/1142
-- broken WAL during upsert
ops = {}
for i = 1,10 do table.insert(ops, {'=', 2, '1234567890'}) end
s:upsert({0}, ops)
--#stop server default
--#start server default
s = box.space.tweedledum

s:drop()
