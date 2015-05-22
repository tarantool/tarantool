net_box = require('net.box')

-- Prepare spaces
box.schema.user.create('test', { password = 'test' })
box.schema.user.grant('test', 'execute,read,write', 'universe')
s = box.schema.space.create('tweedledum', { id = 0 })
index1 = s:create_index('primary', { type = 'tree', parts = { 1, 'str'} })
index2 = s:create_index('secondary', { type = 'tree', unique = false, parts = {2, 'str'}})
function compare(a,b) return a[1] < b[1] end
conn = net_box:new('test:test@' .. box.cfg.listen)
space = conn.space.tweedledum
-- A test case for Bug#729758
-- "SELECT fails with a disjunct and small LIMIT"
-- https://bugs.launchpad.net/tarantool/+bug/729758


space:insert{'Doe', 'Richard'}
space:insert{'Roe', 'Richard'}
space:insert{'Woe', 'Richard'}
space:insert{'Major', 'Tomas'}
space:insert{'Kytes', 'Tomas'}
sorted(space.index.secondary:select('Richard'))


-- A test case for Bug#729879
-- "Zero limit is treated the same as no limit"
-- https://bugs.launchpad.net/tarantool/+bug/729879
sorted(space.index.secondary:select('Richard', { limit = 0 }))
s:truncate()


-- A test case for Bug#730593
-- "Bad data if incomplete tuple"
-- https://bugs.launchpad.net/tarantool/+bug/730593
-- Verify that if there is an index on, say, field 2,
-- we can't insert tuples with cardinality 1 and
-- get away with it.

space:insert{'Britney'}
sorted(space.index.secondary:select('Anything'))
space:insert{'Stephanie'}
sorted(space.index.secondary:select('Anything'))
space:insert{'Spears', 'Britney'}
space:select{'Spears'}
sorted(space.index.secondary:select('Anything'))
sorted(space.index.secondary:select('Britney'))

s.index[0]:select('Spears', { limit = 100, iterator = 'GE' })
s.index[1]:select('Britney', { limit = 100, iterator = 'GE' })

space:delete('Spears')
-- Cleanup
s:truncate()

-- Test composite keys with trees
-- Redefine the second key to be composite
s.index.secondary:alter{unique = true, parts = { 2, 'str', 3, 'str'}}

space:insert{'key1', 'part1', 'part2'}
-- Test a duplicate insert on unique index that once resulted in a crash (bug 926080)
space:replace{'key1', 'part1', 'part2'}
space:insert{'key2', 'part1', 'part2_a'}
space:insert{'key3', 'part1', 'part2_b'}
s.index[1]:select{}
space:select{'key1'}
space:select{'key2'}
space:select{'key3'}
sorted(space.index.secondary:select('part1'))

s.index[1]:select('part1', { limit = 100, iterator = 'GE' })
s.index[0]:select('key2', { limit = 100, iterator = 'GE' })
s.index[1]:select({ 'part1', 'part2_a' }, { limit = 1, iterator = 'GE' })
space:select{'key1'}
space:select{'key2'}
space:select{'key3'}
sorted(space.index.secondary:select('part1'))

space:delete('key1')
space:delete('key2')
space:delete('key3')
s:truncate()

-- check non-unique multipart keys
s.index.primary:alter{type = 'tree', parts = { 1, 'num'}}
s.index.secondary:alter{unique = false}

space:insert{1234567, 'part1', 'part2'}
space:insert{11234567, 'part1', 'part2'}
space:insert{21234567, 'part1', 'part2_a'}
space:insert{31234567, 'part1_a', 'part2'}
space:insert{41234567, 'part1_a', 'part2_a'}

l = {}
for state, v in s:pairs() do table.insert(l, v) end
l

space:select{1234567}
space:select{11234567}
space:select{21234567}
sorted(space.index.secondary:select('part1'))
sorted(space.index.secondary:select('part1_a'))
sorted(space.index.secondary:select('part_none'))

sorted(box.space[0].index[1]:select({'part1', 'part2'}))

sorted(space.index.secondary:select('part1'))
sorted(space.index.secondary:select('part2'))

-- cleanup
space:delete(1234567)
space:delete(11234567)
space:delete(21234567)
space:delete(31234567)
space:delete(41234567)

s:select{}
s:truncate()
s.index.primary:alter{type = 'hash'}
s.index.secondary:alter{type = 'hash', unique = true, parts = { 2, 'str' }}

space:insert{1, 'hello'}
space:insert{2, 'brave'}
space:insert{3, 'new'}
space:insert{4, 'world'}


-- Check how build_indexes() works
--# stop server default
--# start server default
net_box = require('net.box')
conn = net_box:new('test:test@' .. box.cfg.listen)
space = conn.space.tweedledum

s = box.space[0]

-- Bug#929654 - secondary hash index is not built with build_indexes()
sorted(space.index.secondary:select('hello'))
sorted(space.index.secondary:select('brave'))
sorted(space.index.secondary:select('new'))
sorted(space.index.secondary:select('world'))
s:truncate()

-- A test case for: http://bugs.launchpad.net/bugs/735140
-- Partial REPLACE corrupts index.
-- clean data and restart with appropriate config
s.index.primary:alter{parts = {1, 'str'}}
s.index.secondary:alter{type = 'tree', unique = false}

space:insert{'Spears', 'Britney'}
space:select{'Spears'}
sorted(space.index.secondary:select('Britney'))
-- try to insert the incoplete tuple
space:replace{'Spears'}
-- check that nothing has been updated
space:select{'Spears'}
-- cleanup
space:delete('Spears')

-- Test retrieval of duplicates via a secondary key
s.index.primary:alter{parts = { 1, 'num'}}

space:insert{1, 'duplicate one'}
space:insert{2, 'duplicate one'}
space:insert{3, 'duplicate one'}
space:insert{4, 'duplicate one'}
space:insert{5, 'duplicate one'}
space:insert{6, 'duplicate two'}
space:insert{7, 'duplicate two'}
space:insert{8, 'duplicate two'}
space:insert{9, 'duplicate two'}
space:insert{10, 'duplicate two'}
space:insert{11, 'duplicate three'}
space:insert{12, 'duplicate three'}
space:insert{13, 'duplicate three'}
space:insert{14, 'duplicate three'}
space:insert{15, 'duplicate three'}


sorted(space.index.secondary:select('duplicate one'))
sorted(space.index.secondary:select('duplicate two'))
sorted(space.index.secondary:select('duplicate three'))

space:delete(1)
space:delete(2)
space:delete(3)
space:delete(4)
space:delete(5)
space:delete(6)
space:delete(7)
space:delete(8)
space:delete(9)
space:delete(10)
space:delete(11)
space:delete(12)
space:delete(13)
space:delete(14)
space:delete(15)

-- Check min() and max() functions
space:insert{1, 'Aardvark '}
space:insert{2, 'Bilimbi'}
space:insert{3, 'Creature '}

s.index[1]:select{}
s.index[0]:min()
s.index[0]:max()
s.index[1]:min()
s.index[1]:max()

space:delete(1)
space:delete(2)
space:delete(3)

box.schema.user.drop('test')
s:drop()
