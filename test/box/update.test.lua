s = box.schema.space.create('tweedledum')
index = s:create_index('pk')

-- test delete field
s:insert{1000001, 1000002, 1000003, 1000004, 1000005}
s:update({1000001}, {{'#', 1, 1}})
s:update({1000001}, {{'#', 1, "only one record please"}})
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
-- decimal
decimal = require('decimal')
s:replace{0, decimal.new("2.000")}
s:update({0}, {{'+', 2, 2ULL}}) -- decimal + unsigned = decimal 4.000
s:update({0}, {{'+', 2, -4LL}}) -- decimal + signed = decimal 0.000
s:update({0}, {{'+', 2, 2}})  -- decimal + number = decimal 2.000
s:update({0}, {{'-', 2, 2}}) -- decimal - number = decimal 0.000
s:update({0}, {{'-', 2, ffi.new('float', 2)}}) -- decimal - float = decimal -2.000
s:update({0}, {{'-', 2, ffi.new('double', 2)}}) -- decimal - double = decimal -4.000
s:update({0}, {{'+', 2, decimal.new(4)}}) -- decimal + decimal = decimal 0.000
s:update({0}, {{'-', 2, decimal.new(2)}}) -- decimal - decimal = decimal -2.000
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

-- https://github.com/tarantool/tarantool/issues/1854
s:get{0}
s:update({0}, {})

--#stop server default
--#start server default
s = box.space.tweedledum

--
-- gh-2036: msgpackffi doesn't support __serialize hint
--
map = setmetatable({}, { __serialize = 'map' })
t = box.tuple.new({1, 2, 3})
s:replace({1, 2, 3})

t:update({{'=', 3, map}})
s:update(1, {{'=', 3, map}})

s:drop()

--
-- gh-1261: update by JSON path.
--
format = {}
format[1] = {'field1', 'unsigned'}
format[2] = {'f', 'map'}
format[3] = {'g', 'array'}
s = box.schema.create_space('test', {format = format})
pk = s:create_index('pk')
t = {}
t[1] = 1
t[2] = {                            \
    a = 100,                        \
    b = 200,                        \
    c = {                           \
        d = 400,                    \
        e = 500,                    \
        f = {4, 5, 6, 7, 8},        \
        g = {k = 600, l = 700}      \
    },                              \
    m = true,                       \
    g = {800, 900}                  \
};                                  \
t[3] = {                            \
    100,                            \
    200,                            \
    {                               \
        {300, 350},                 \
        {400, 450}                  \
    },                              \
    {a = 500, b = 600},             \
    {c = 700, d = 800}              \
}
t = s:insert(t)

t4_array = t:update({{'!', 4, setmetatable({}, {__serialize = 'array'})}})
t4_map = t:update({{'!', 4, setmetatable({}, {__serialize = 'map'})}})

t
--
-- At first, test simple non-intersected paths.
--

--
-- !
--
t:update({{'!', 'f.c.f[1]', 3}, {'!', '[3][1]', {100, 200, 300}}})
t:update({{'!', 'f.g[3]', 1000}})
t:update({{'!', 'g[6]', 'new element'}})
t:update({{'!', 'f.e', 300}, {'!', 'g[4].c', 700}})
t:update({{'!', 'f.c.f[2]', 4.5}, {'!', 'g[3][2][2]', 425}})
t2 = t:update({{'!', 'g[6]', {100}}})
-- Test single element array update.
t2:update({{'!', 'g[6][2]', 200}})
t2:update({{'!', 'g[6][1]', 50}})
-- Test empty array/map.
t4_array:update({{'!', '[4][1]', 100}})
t4_map:update({{'!', '[4].a', 100}})
-- Test errors.
t:update({{'!', 'a', 100}}) -- No such field.
t:update({{'!', 'f.a', 300}}) -- Key already exists.
t:update({{'!', 'f.c.f[0]', 3.5}}) -- No such index, too small.
t:update({{'!', 'f.c.f[100]', 100}}) -- No such index, too big.
t:update({{'!', 'g[4][100]', 700}}) -- Insert index into map.
t:update({{'!', 'g[1][1]', 300}})
t:update({{'!', 'f.g.a', 700}}) -- Insert key into array.
t:update({{'!', 'f.g[1].a', 700}})
t:update({{'!', 'f[*].k', 20}}) -- 'Any' is not considered valid JSON.
-- JSON error after the not existing field to insert.
t:update({{'!', '[2].e.100000', 100}})
-- Correct JSON, but next to last field does not exist. '!' can't
-- create the whole path.
t:update({{'!', '[2].e.f', 100}})

--
-- =
--
-- Set existing fields.
t:update({{'=', 'f.a', 150}, {'=', 'g[3][1][2]', 400}})
t:update({{'=', 'f', {a = 100, b = 200}}})
t:update({{'=', 'g[4].b', 700}})
-- Insert via set.
t:update({{'=', 'f.e', 300}})
t:update({{'=', 'f.g[3]', 1000}})
t:update({{'=', 'f.g[1]', 0}})
-- Test empty array/map.
t4_array:update({{'=', '[4][1]', 100}})
t4_map:update({{'=', '[4]["a"]', 100}})
-- Test errors.
t:update({{'=', 'f.a[1]', 100}})
t:update({{'=', 'f.a.k', 100}})
t:update({{'=', 'f.c.f[1]', 100}})
t:update({{'=', 'f.c.f[100]', 100}})
t:update({{'=', '[2].c.f 1 1 1 1', 100}})

--
-- #
--
t:update({{'#', '[2].b', 1}})
t:update({{'#', 'f.c.f[1]', 1}})
t:update({{'#', 'f.c.f[1]', 2}})
t:update({{'#', 'f.c.f[1]', 100}})
t:update({{'#', 'f.c.f[5]', 1}})
t:update({{'#', 'f.c.f[5]', 2}})
-- Test errors.
t:update({{'#', 'f.h', 1}})
t:update({{'#', 'f.c.f[100]', 1}})
t:update({{'#', 'f.b', 2}})
t:update({{'#', 'f.b', 0}})
t:update({{'#', 'f', 0}})

--
-- Scalar operations.
--
t:update({{'+', 'f.a', 50}})
t:update({{'-', 'f.c.f[1]', 0.5}})
t:update({{'&', 'f.c.f[2]', 4}})
t2 = t:update({{'=', 4, {str = 'abcd'}}})
t2:update({{':', '[4].str', 2, 2, 'e'}})
-- Test errors.
t:update({{'+', 'g[3]', 50}})
t:update({{'+', '[2].b.......', 100}})
t:update({{'+', '[2].b.c.d.e', 100}})
t:update({{'-', '[2][*]', 20}})

-- Vinyl normalizes field numbers. It should not touch paths,
-- and they should not affect squashing.
format = {}
format[1] = {'field1', 'unsigned'}
format[2] = {'field2', 'any'}
vy_s = box.schema.create_space('test2', {engine = 'vinyl', format = format})
pk = vy_s:create_index('pk')
_ = vy_s:replace(t)

box.begin()
-- Use a scalar operation, only they can be squashed.
vy_s:upsert({1, 1}, {{'+', 'field2.c.f[1]', 1}})
vy_s:upsert({1, 1}, {{'+', '[3][3][1][1]', 1}})
box.commit()

vy_s:select()
vy_s:drop()

s:drop()
