env = require('test_run')
test_run = env.new()
test_run:cmd("push filter ".."'\\.lua.*:[0-9]+: ' to '.lua...\"]:<line>: '")

space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'string'}, unique = true })
tmp = space:create_index('minmax', { type = 'tree', parts = {2, 'string', 3, 'string'}, unique = true })

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
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
tmp = space:create_index('minmax', { type = 'tree', parts = {2, 'string', 3, 'string'}, unique = false })

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
tmp = space:create_index('primary', { type  = 'tree', parts = {1, 'unsigned'}, unique = true })

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

-- gh-2258: Incomprehensive failure of auto_increment in absence of indices
space.index.primary:drop()
space:auto_increment{'a'}
space:get({1})
space:select()
space:update({1}, {})
space:upsert({1}, {})
space:delete({1})
space:bsize()
space:count()
space:len()
space:pairs():totable()
space:drop()

--
-- Tests for lua idx:count()
--
-- https://blueprints.launchpad.net/tarantool/+spec/lua-builtin-size-of-subtree
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
tmp = space:create_index('i1', { type = 'tree', parts = {2, 'unsigned', 3, 'unsigned'}, unique = false })
space:insert{1, 1, 1}
space:insert{2, 2, 0}
space:insert{3, 2, 1}
space:insert{4, 3, 0}
space:insert{5, 3, 1}
space:insert{6, 3, 2}
space.index['i1']:count()
space:count()
space.index['i1']:count(1)
space:count(1)
space.index['i1']:count(1)
space.index['i1']:count(2, { iterator = 'LE' })
space.index['i1']:count(2, { iterator = 'GE' })
space:count(2, { iterator = 'GE' })
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
status, msg = pcall(function() space.index['i1']:count(function() end) end)
status
msg:match("can not encode Lua type: 'function'")
space:drop()

--
-- Tests for lua tuple:transform()
--
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'hash', parts = {1, 'string'}, unique = true })
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
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}, unique = true })
push_collection = require('push')

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
tmp = space:create_index('primary', { type = 'tree', parts = {3, 'unsigned', 2, 'unsigned'}, unique = true })

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
index_random_test = require('index_random_test')
space = box.schema.space.create('tweedledum')
tmp = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}, unique = true })
tmp = space:create_index('secondary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
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

-------------------------------------------------------------------------------
-- space:format()
-------------------------------------------------------------------------------

space = box.schema.space.create('tweedledum')
pk = space:create_index('primary')

space:format()
box.schema.space.format(space.id)
box.space._space:get(space.id)[7]

space:format({{name = 'id', type = 'unsigned'}})
space:format()
box.schema.space.format(space.id)
box.space._space:get(space.id)[7]

space:format({})
space:format()
box.schema.space.format(space.id)
box.space._space:get(space.id)[7]

space:drop()

-------------------------------------------------------------------------------
-- Invalid arguments
-------------------------------------------------------------------------------

space = box.schema.space.create('tweedledum')
pk = space:create_index('primary')

space.len()
space.count({}, {iterator = 'EQ'})
space.bsize()
space.get({1})
space.select({}, {iterator = 'GE'})
space.insert({1, 2, 3})
space.replace({1, 2, 3})
space.put({1, 2, 3})
space.update({1}, {})
space.upsert({1, 2, 3}, {})
space.delete({1})
space.auto_increment({'hello'})
space.pairs({}, {iterator = 'EQ'})
space.truncate()
space.format({})
space.drop()
space.rename()
space.create_index('secondary')
space.run_triggers(false)

pk.len()
pk.bsize()
pk.min()
pk.min({})
pk.max()
pk.max({})
pk.random(42)
pk.pairs({}, {iterator = 'EQ'})
pk.count({}, {iterator = 'EQ'})
pk.get({1})
pk.select({}, {iterator = 'GE'})
pk.update({1}, {})
pk.delete({1})
pk.drop()
pk.rename("newname")
pk.alter({})

space:drop()
pk = nil
space = nil

test_run:cmd("clear filter")
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4
