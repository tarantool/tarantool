test_run = require('test_run').new()
test_run:cmd("push filter '(error: .builtin/.*[.]lua):[0-9]+' to '\\1'")
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'str'}, unique = true })
tmp = space:create_index('minmax', { type = 'tree', parts = {2, 'str', 3, 'str'}, unique = true })

space:insert{'brave', 'new', 'world'}
space:insert{'hello', 'old', 'world'}
space.index['minmax']:min()
space.index['minmax']:max()
space.index['minmax']:get{'new', 'world'}

-- A test case for Bug #904208
-- "assert failed, when key cardinality is greater than index cardinality"
--  https://bugs.launchpad.net/tarantool/+bug/904208

space.index['minmax']:get{'new', 'world', 'order'}
space:delete{'brave'}

-- A test case for Bug #902091
-- "Positioned iteration over a multipart index doesn't work"
-- https://bugs.launchpad.net/tarantool/+bug/902091

space:insert{'item 1', 'alabama', 'song'}
space.index['minmax']:get{'alabama'}
space:insert{'item 2', 'california', 'dreaming '}
space:insert{'item 3', 'california', 'uber alles'}
space:insert{'item 4', 'georgia', 'on my mind'}
iter, param, state = space.index['minmax']:pairs('california', { iterator =  box.index.GE })
state, v = iter(param, state)
v
state, v = iter(param, state)
v
space:delete{'item 1'}
space:delete{'item 2'}
space:delete{'item 3'}
space:delete{'item 4'}
space:truncate()

--
-- Test that we print index number in error ER_INDEX_VIOLATION
--
space:insert{'1', 'hello', 'world'}
space:insert{'2', 'hello', 'world'}
space:drop()

--
-- Check range scan over multipart keys
--
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'num'}, unique = true })
tmp = space:create_index('minmax', { type = 'tree', parts = {2, 'str', 3, 'str'}, unique = false })

space:insert{1234567, 'new', 'world'}
space:insert{0, 'of', 'puppets'}
space:insert{00000001ULL, 'of', 'might', 'and', 'magic'}
space.index['minmax']:select('of', { limit = 2, iterator = 'GE' })
space.index['minmax']:select('of', { limit = 2, iterator = 'LE' })
space:truncate()

--
-- A test case for Bug#1060967: truncation of 64-bit numbers
--

space:insert{2^51, 'hello', 'world'}
space.index['primary']:get{2^51}
space:drop()

--
-- Lua 64bit numbers support
--
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type  = 'tree', parts = {1, 'num'}, unique = true })

space:insert{tonumber64('18446744073709551615'), 'magic'}
tuple = space.index['primary']:get{tonumber64('18446744073709551615')}
num = tuple[1]
num
type(num) == 'cdata'
num == tonumber64('18446744073709551615')
num = tuple[1]
num == tonumber64('18446744073709551615')
space:delete{18446744073709551615ULL}
space:insert{125ULL, 'magic'}
tuple = space.index['primary']:get{125}
tuple2 = space.index['primary']:get{125LL}
num = tuple[1]
num2 = tuple2[1]
num, num2
type(num) == 'number'
type(num2) == 'number'
num == tonumber64('125')
num2 == tonumber64('125')
space:truncate()

--
-- Tests for lua box.auto_increment with NUM keys
--
-- lua box.auto_increment() with NUM keys testing
space:auto_increment{'a'}
space:insert{tonumber64(5)}
space:auto_increment{'b'}
space:auto_increment{'c'}

space:drop()

--
-- Lua select_reverse_range
--
-- lua select_reverse_range() testing
-- https://blueprints.launchpad.net/tarantool/+spec/backward-tree-index-iterator
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'num'}, unique = true })
tmp = space:create_index('range', { type = 'tree', parts = {2, 'num', 1, 'num'}, unique = true })

space:insert{0, 0}
space:insert{1, 0}
space:insert{2, 0}
space:insert{3, 0}
space:insert{4, 0}
space:insert{5, 0}
space:insert{6, 0}
space:insert{7, 0}
space:insert{8, 0}
space:insert{9, 0}
space.index['range']:select({}, { limit = 10, iterator = 'GE' })
space.index['range']:select({}, { limit = 10, iterator = 'LE' })
space.index['range']:select({}, { limit = 4, iterator = 'LE' })
space:drop()

--
-- Tests for box.index iterators
--
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'str'}, unique = true })
tmp = space:create_index('i1', { type = 'tree', parts = {2, 'str', 3, 'str'}, unique = true })

pid = 1
tid = 999
env = require('test_run')
test_run = env.new()
test_run:cmd("setopt delimiter ';'")
for sid = 1, 2 do
    for i = 1, 3 do
        space:insert{'pid_'..pid, 'sid_'..sid, 'tid_'..tid}
        pid = pid + 1
        tid = tid - 1
    end
end;
test_run:cmd("setopt delimiter ''");

index = space.index['i1']

t = {}
for state, v in index:pairs('sid_1', { iterator = 'GE' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_2', { iterator = 'LE' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_1', { iterator = 'EQ' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_1', { iterator = 'REQ' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_2', { iterator = 'EQ' }) do table.insert(t, v) end
t
t = {}
for state, v in index:pairs('sid_2', { iterator = 'REQ' }) do table.insert(t, v) end
t
t = {}


index:pairs('sid_t', { iterator = 'wrong_iterator_type' })

index = nil
space:drop()

--
-- Tests for lua idx:count()
--
-- https://blueprints.launchpad.net/tarantool/+spec/lua-builtin-size-of-subtree
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'num'}, unique = true })
tmp = space:create_index('i1', { type = 'tree', parts = {2, 'num', 3, 'num'}, unique = false })
space:insert{1, 1, 1}
space:insert{2, 2, 0}
space:insert{3, 2, 1}
space:insert{4, 3, 0}
space:insert{5, 3, 1}
space:insert{6, 3, 2}
space.index['i1']:count()
space.index['i1']:count(1)
space.index['i1']:count(1)
space.index['i1']:count(2, { iterator = 'LE' })
space.index['i1']:count(2, { iterator = 'GE' })
space.index['i1']:count({2, 0}, { iterator = 'LE' })
space.index['i1']:count({2, 1}, { iterator = 'GE' })

space.index['i1']:count(2)
space.index['i1']:count({2, 1})
space.index['i1']:count({2, 2})
space.index['i1']:count(3)
space.index['i1']:count({3, 3})
-- Returns total number of records
-- https://github.com/tarantool/tarantool/issues/46
space.index['i1']:count()
-- Test cases for #123: box.index.count does not check arguments properly
space.index['i1']:count(function() end)
space:drop()

--
-- Tests for lua tuple:transform()
--
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'str'}, unique = true })
t = space:insert{'1', '2', '3', '4', '5', '6', '7'}
t:transform(8, 0, '8', '9', '100')
t:transform(1, 1)
t:transform(2, 4)
t:transform(-1, 1)
t:transform(-3, 2)
t:transform(1, 0, 'A')
t:transform(-1, 0, 'A')
t:transform(1, 1, 'A')
t:transform(-1, 1, 'B')
t:transform(1, 2, 'C')
t:transform(3, 0, 'hello')
t:transform(1, -1, 'C')
t:transform(1, 100)
t:transform(-100, 1)
t:transform(1, 3, 1, 2, 3)
t:transform(4, 1, tonumber64(4))
t:transform(1, 1, {})
space:truncate()

--
-- Tests for OPENTAR-64 - a limitation for the second argument to tuple:transform
--

-- 50K is enough for everyone
n = 2000
tab = {}; for i=1,n,1 do table.insert(tab, i) end
t = box.tuple.new(tab)
t:transform(1, n - 1)
t = nil

--
-- Tests for lua tuple:find() and tuple:findall()
--
-- First space for hash_str tests

t = space:insert{'A', '2', '3', '4', '3', '2', '5', '6', '3', '7'}
t:find('2')
t:find('4')
t:find('5')
t:find('A')
t:find('0')

t:findall('A')
t:findall('2')
t:findall('3')
t:findall('0')

t:find(2, '2')
t:find(89, '2')
t:findall(4, '3')

t = space:insert{'Z', '2', 2, 3, tonumber64(2)}
t:find(2)
t:findall(tonumber64(2))
t:find('2')
space:drop()

-- A test case for Bug #1038784
-- transform returns wrong tuple and put broken reply into socket
-- http://bugs.launchpad.net/tarantool/+bug/1038784
--  https://bugs.launchpad.net/tarantool/+bug/1006354
--  lua box.auto_increment() testing

space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'num'}, unique = true })
dofile('push.lua')

push_collection(space, 0, 1038784, 'hello')
push_collection(space, 0, 1038784, 'hello')
push_collection(space, 0, 1038784, 'hello')

push_collection(space, 1, 1038784, 'hi')
push_collection(space, 2, 1038784, 'hi')
push_collection(space, 2, 1038784, 'hi')

push_collection(space, 5, 1038784, 'hey')
push_collection(space, 5, 1038784, 'hey')
push_collection(space, 5, 1038784, 'hey')
push_collection(space, 5, 1038784, 'hey')

-- # lua box.auto_increment() testing
-- # http://bugs.launchpad.net/tarantool/+bug/1006354
--
-- Tests for lua box.auto_increment
--
space:truncate()
space:auto_increment{'a'}
space:insert{5}
space:auto_increment{'b'}
space:auto_increment{'c'}
space:auto_increment{'d'}
space:drop()

-- A test case for Bug #1042798
-- Truncate hangs when primary key is not in linear or starts at the first field
-- https://bugs.launchpad.net/tarantool/+bug/1042798
--
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'tree', parts = {3, 'num', 2, 'num'}, unique = true })

-- Print key fields in pk
space.index['primary'].parts
space:insert{1, 2, 3, 4}
space:insert{10, 20, 30, 40}
space:insert{20, 30, 40, 50}
space.index['primary']:select{}

-- Truncate must not hang
space:truncate()

-- Empty result
space.index['primary']:select{}
space:drop()
    
--
-- index:random test
-- 
dofile('index_random_test.lua')
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'num'}, unique = true })
tmp = space:create_index('secondary', { type = 'hash', parts = {1, 'num'}, unique = true })
-------------------------------------------------------------------------------
-- TreeIndex::random()
-------------------------------------------------------------------------------

index_random_test(space, 'primary')

-------------------------------------------------------------------------------
-- HashIndex::random()
-------------------------------------------------------------------------------

index_random_test(space, 'secondary')

space:drop()
space = nil

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
