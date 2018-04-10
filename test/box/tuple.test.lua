-- box.tuple test
env = require('test_run')
test_run = env.new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")

-- Test box.tuple:slice()
t=box.tuple.new{'0', '1', '2', '3', '4', '5', '6', '7'}
t:slice(0)
t:slice(-1)
t:slice(1)
t:slice(-1, -1)
t:slice(-1, 1)
t:slice(1, -1)
t:slice(1, 3)
t:slice(7)
t:slice(8)
t:slice(9)
t:slice(100500)
t:slice(9, -1)
t:slice(6, -1)
t:slice(4, 4)
t:slice(6, 4)
t:slice(0, 0)
t:slice(9, 10)
t:slice(-7)
t:slice(-8)
t:slice(-9)
t:slice(-100500)
t:slice(500, 700)
--  box.tuple.new test
box.tuple.new()
box.tuple.new(1)
box.tuple.new('string')
box.tuple.new(tonumber64('18446744073709551615'))
box.tuple.new{tonumber64('18446744073709551615'), 'string', 1}
--  A test case for Bug#1131108 'incorrect conversion from boolean lua value to tarantool tuple'

function bug1075677() local range = {} table.insert(range, 1>0) return range end
box.tuple.new(bug1075677())
bug1075677=nil

-- boolean values in a tuple
box.tuple.new(false)
box.tuple.new({false})

-- tuple:bsize()

t = box.tuple.new('abc')
t
t:bsize()

--
-- Test cases for #106 box.tuple.new fails on multiple items
--
box.tuple.new()
box.tuple.new{}

box.tuple.new(1)
box.tuple.new{1}

box.tuple.new(1, 2, 3, 4, 5)
box.tuple.new{1, 2, 3, 4, 5}

box.tuple.new({'a', 'b'}, {'c', 'd'}, {'e', 'f'})
box.tuple.new{{'a', 'b'}, {'c', 'd'}, {'e', 'f'}}

box.tuple.new({1, 2}, 'x', 'y', 'z', {c = 3, d = 4}, {e = 5, f = 6})
box.tuple.new{{1, 2}, 'x', 'y', 'z', {c = 3, d = 4}, {e = 5, f = 6}}

box.tuple.new('x', 'y', 'z', {1, 2}, {c = 3, d = 4}, {e = 5, f = 6})
box.tuple.new{'x', 'y', 'z', {1, 2}, {c = 3, d = 4}, {e = 5, f = 6}}

t=box.tuple.new{'a','b','c'}
t:totable()
t:unpack()
t:totable(1)
t:unpack(1)
t:totable(2)
t:unpack(2)
t:totable(1, 3)
t:unpack(1, 3)
t:totable(2, 3)
t:unpack(2, 3)
t:totable(2, 4)
t:unpack(2, 4)
t:totable(nil, 2)
t:unpack(nil, 2)
t:totable(2, 1)
t:unpack(2, 1)

t:totable(0)
t:totable(1, 0)

--
-- Check that tuple:totable correctly sets serializer hints
--
box.tuple.new{1, 2, 3}:totable()
getmetatable(box.tuple.new{1, 2, 3}:totable()).__serialize

--  A test case for the key as an tuple
space = box.schema.space.create('tweedledum')
index = space:create_index('primary')
space:truncate()
t=space:insert{0, 777, '0', '1', '2', '3'}
t
space:replace(t)
space:replace{777, { 'a', 'b', 'c', {'d', 'e', t}}}
--  A test case for tuple:totable() method
t=space:get{777}:totable()
t[2], t[3], t[4], t[5]
space:truncate()
--  A test case for Bug#1119389 '(lbox_tuple_index) crashes on 'nil' argument'
t=space:insert{0, 8989}
t[nil]

--------------------------------------------------------------------------------
-- test tuple:next
--------------------------------------------------------------------------------

t = box.tuple.new({'a', 'b', 'c'})
state, val = t:next()
state, val
state, val = t:next(state)
state, val
state, val = t:next(state)
state, val
state, val = t:next(state)
state, val
t:next(nil)
t:next(0)
t:next(1)
t:next(2)
t:next(3)
t:next(4)
t:next(-1)
t:next("fdsaf")

box.tuple.new({'x', 'y', 'z'}):next()

t=space:insert{1953719668}
t:next(1684234849)
t:next(1)
t:next(nil)
t:next(t:next())

--------------------------------------------------------------------------------
-- test tuple:pairs
--------------------------------------------------------------------------------

ta = {} for k, v in t:pairs() do table.insert(ta, v) end
ta
t=space:replace{1953719668, 'another field'}
ta = {} for k, v in t:pairs() do table.insert(ta, v) end
ta
t=space:replace{1953719668, 'another field', 'one more'}
ta = {} for k, v in t:pairs() do table.insert(ta, v) end
ta
t=box.tuple.new({'a', 'b', 'c', 'd'})
ta = {} for it,field in t:pairs() do table.insert(ta, field); end
ta

t = box.tuple.new({'a', 'b', 'c'})
gen, init, state = t:pairs()
gen, init, state
state, val = gen(init, state)
state, val
state, val = gen(init, state)
state, val
state, val = gen(init, state)
state, val
state, val = gen(init, state)
state, val

r = {}
for _state, val in t:pairs() do table.insert(r, val) end
r

r = {}
for _state, val in t:pairs() do table.insert(r, val) end
r

r = {}
for _state, val in t:pairs(1) do table.insert(r, val) end
r

r = {}
for _state, val in t:pairs(3) do table.insert(r, val) end
r

r = {}
for _state, val in t:pairs(10) do table.insert(r, val) end
r

r = {}
for _state, val in t:pairs(nil) do table.insert(r, val) end
r

t:pairs(nil)
t:pairs("fdsaf")

--------------------------------------------------------------------------------
-- test tuple:find
--------------------------------------------------------------------------------

env = require('test_run')
test_run = env.new()
test_run:cmd("setopt delimiter ';'")
t = box.tuple.new({'a','b','c','a', -1, 0, 1, 2, true, 9223372036854775807ULL,
    -9223372036854775807LL});
test_run:cmd("setopt delimiter ''");
t:find('a')
t:find(1, 'a')
t:find('c')
t:find('xxxxx')
t:find(1, 'xxxxx')
t:findall('a')
t:findall(1, 'a')
t:findall('xxxxx')
t:findall(1, 'xxxxx')
t:find(100, 'a')
t:findall(100, 'a')
t:find(100, 'xxxxx')
t:findall(100, 'xxxxx')

---
-- Lua type coercion
---
t:find(2)
t:findall(2)
t:find(2ULL)
t:findall(2ULL)
t:find(2LL)
t:findall(2LL)
t:find(2)
t:findall(2)

t:find(-1)
t:findall(-1)
t:find(-1LL)
t:findall(-1LL)

t:find(true)
t:findall(true)

t:find(9223372036854775807LL)
t:findall(9223372036854775807LL)
t:find(9223372036854775807ULL)
t:findall(9223372036854775807ULL)
t:find(-9223372036854775807LL)
t:findall(-9223372036854775807LL)

--------------------------------------------------------------------------------
-- test tuple:update
--------------------------------------------------------------------------------

-- see box/update.test.lua for more test cases

t = box.tuple.new({'a', 'b', 'c', 'd', 'e'})
t:update()
t:update(10)
t:update({})
t:update({{ '!', -1, 'f'}})
t:update({{ '#', 4, 1}})

t
t = nil

-- gh-2454 Regression in msgpack
t = box.tuple.new(require('yaml').decode("[17711728, {1000: 'xxx'}]"))
t:update({{'=', 2, t[2]}})
t
t = nil
--------------------------------------------------------------------------------
-- test msgpack.encode + tuple
--------------------------------------------------------------------------------

msgpack = require('msgpack')
encode_load_metatables = msgpack.cfg.encode_load_metatables

-- disable __serialize hook to test internal on_encode hook
msgpack.cfg{encode_load_metatables = false}
msgpackffi = require('msgpackffi')

t = box.tuple.new({'a', 'b', 'c'})
msgpack.decode(msgpackffi.encode(t))
msgpack.decode(msgpack.encode(t))
msgpack.decode(msgpackffi.encode({1, {'x', 'y', t, 'z'}, 2, 3}))
msgpack.decode(msgpack.encode({1, {'x', 'y', t, 'z'}, 2, 3}))

-- restore configuration
msgpack.cfg{encode_load_metatables = encode_load_metatables}

-- gh-738: Serializer hints are unclear
t = box.tuple.new({1, 2, {}})
map = t[3]
getmetatable(map) ~= nil
map
map['test'] = 48
map
getmetatable(map) == nil

-- gh-1189: tuple is not checked as first argument
t = box.tuple.new({1, 2, {}})
t.bsize()
t.find(9223372036854775807LL)
t.findall(9223372036854775807LL)
t.update()
t.upsert()
t = nil

space:drop()

-- gh-1266: luaL_convertfield crashes on ffi.typeof()
ffi = require('ffi')
ffi.typeof('struct tuple')

-- gh-1345: lbox_tuple_new() didn't check result of box_tuple_new() for NULL
-- try to allocate 100Mb tuple and checked that server won't crash
box.tuple.new(string.rep('x', 100 * 1024 * 1024)) ~= nil
collectgarbage('collect') -- collect huge string

-- testing tostring
test_run:cmd("setopt delimiter ';'")
null = nil
t = box.tuple.new({1, -2, 1.2, -1.2}, 'x', 'y', 'z', null, true, false,
    {bin = "\x08\x5c\xc2\x80\x12\x2f",
    big_num = tonumber64('18446744073709551615'),
    map = {key = "value"},
    double=1.0000000001,
    utf8="Кудыкины горы"});
tostring(t);
t;
test_run:cmd("setopt delimiter ''");

--
-- gh-1014: tuple field names and tuple methods aliases.
--
t = box.tuple.new({1, 2, 3})
box.tuple.next == t.next
box.tuple.ipairs == t.ipairs
box.tuple.pairs == t.pairs
box.tuple.slice == t.slice
box.tuple.transform == t.transform
box.tuple.find == t.find
box.tuple.findall == t.findall
box.tuple.unpack == t.unpack
box.tuple.totable == t.totable
box.tuple.update == t.update
box.tuple.upsert == t.upsert
box.tuple.bsize == t.bsize

--
-- gh-3282: space:frommap().
--
format = {}
format[1] = {name = 'aaa', type = 'unsigned'}
format[2] = {name = 'bbb', type = 'unsigned'}
format[3] = {name = 'ccc', type = 'unsigned'}
format[4] = {name = 'ddd', type = 'unsigned'}
s = box.schema.create_space('test', {format = format})
s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = 4})
s:frommap({ddd = 1, aaa = 2, bbb = 3})
s:frommap({ddd = 1, aaa = 2, ccc = 3, eee = 4})
s:frommap()
s:frommap({})
s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = 4}, {table = true})
s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = 4}, {table = false})
s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = box.NULL})
s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = 4}, {dummy = true})
_ = s:create_index('primary', {parts = {'aaa'}})
tuple = s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = 4})
s:replace(tuple)
s:drop()
_, err = s:frommap({ddd = 1, aaa = 2, ccc = 3, bbb = 4})
assert(err ~= nil)

test_run:cmd("clear filter")
