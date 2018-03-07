test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')
inspector:cmd("push filter '"..engine.."' to 'engine'")

-- iterator (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {} for state, v in index:pairs({}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(44), {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(44), {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(77), {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(tostring(77), {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()


-- iterator (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:replace({key}) end
t = {} for state, v in index:pairs({}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(44, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(44, {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(77, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(77, {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()


-- iterator multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:replace({key, key}) end
t = {} for state, v in index:pairs({}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({44, 44}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({44, 44}, {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({77, 77}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs({77, 77}, {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()

-- iterator with tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:replace({tostring(key)}) end
t = {} for state, v in index:pairs(box.tuple.new{}, {iterator = 'ALL'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new{}, {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new(tostring(44)), {iterator = 'GE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new(tostring(44)), {iterator = 'GT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new{}, {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new(tostring(77)), {iterator = 'LE'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new{}, {iterator = 'LT'}) do table.insert(t, v) end
t
t = {} for state, v in index:pairs(box.tuple.new(tostring(77)), {iterator = 'LT'}) do table.insert(t, v) end
t
space:drop()

iterate = dofile('utils.lua').iterate

inspector:cmd("push filter '(error: .builtin/.*[.]lua):[0-9]+' to '\\1'")
# Tree single-part unique

space = box.schema.space.create('tweedledum', { engine = engine })
idx1 = space:create_index('primary', { type = 'tree', parts = {1, 'string'}, unique = true})
-- Tree single-part non-unique
idx2 = space:create_index('i1', { type = 'tree', parts = {2, 'string'}, unique = false})
-- Tree multi-part unique
idx3 = space:create_index('i2', { type = 'tree', parts = {2, 'string', 3, 'string'}, unique = true})
-- Tree multi-part non-unique
idx4 = space:create_index('i3', { type = 'tree', parts = {3, 'string', 4, 'string'}, unique = false })

space:insert{'pid_001', 'sid_001', 'tid_998', 'a'}
space:insert{'pid_002', 'sid_001', 'tid_997', 'a'}
space:insert{'pid_003', 'sid_002', 'tid_997', 'b'}
space:insert{'pid_005', 'sid_002', 'tid_996', 'b'}
space:insert{'pid_007', 'sid_003', 'tid_996', 'a'}
space:insert{'pid_011', 'sid_004', 'tid_996', 'c'}
space:insert{'pid_013', 'sid_005', 'tid_996', 'b'}
space:insert{'pid_017', 'sid_006', 'tid_996', 'a'}
space:insert{'pid_019', 'sid_005', 'tid_995', 'a'}
space:insert{'pid_023', 'sid_005', 'tid_994', 'a'}

-------------------------------------------------------------------------------
-- Iterator: tree single-part unique
-------------------------------------------------------------------------------

iterate('tweedledum', 'primary', 0, 1)
iterate('tweedledum', 'primary', 0, 1, box.index.ALL)
iterate('tweedledum', 'primary', 0, 1, box.index.EQ)
iterate('tweedledum', 'primary', 0, 1, box.index.REQ)
iterate('tweedledum', 'primary', 0, 1, box.index.GE)
iterate('tweedledum', 'primary', 0, 1, box.index.GT)
iterate('tweedledum', 'primary', 0, 1, box.index.LE)
iterate('tweedledum', 'primary', 0, 1, box.index.LT)
iterate('tweedledum', 'primary', 0, 1, box.index.EQ, 'pid_003')
iterate('tweedledum', 'primary', 0, 1, box.index.REQ, 'pid_003')
iterate('tweedledum', 'primary', 0, 1, box.index.EQ, 'pid_666')
iterate('tweedledum', 'primary', 0, 1, box.index.REQ, 'pid_666')
iterate('tweedledum', 'primary', 0, 1, box.index.GE, 'pid_001')
iterate('tweedledum', 'primary', 0, 1, box.index.GT, 'pid_001')
iterate('tweedledum', 'primary', 0, 1, box.index.GE, 'pid_999')
iterate('tweedledum', 'primary', 0, 1, box.index.GT, 'pid_999')
iterate('tweedledum', 'primary', 0, 1, box.index.LE, 'pid_002')
iterate('tweedledum', 'primary', 0, 1, box.index.LT, 'pid_002')
iterate('tweedledum', 'primary', 0, 1, box.index.LE, 'pid_000')
iterate('tweedledum', 'primary', 0, 1, box.index.LT, 'pid_000')

-------------------------------------------------------------------------------
-- Iterator: tree single-part non-unique
-------------------------------------------------------------------------------

iterate('tweedledum', 'i1', 1, 2, box.index.ALL)
iterate('tweedledum', 'i1', 1, 2, box.index.EQ)
iterate('tweedledum', 'i1', 1, 2, box.index.REQ)
iterate('tweedledum', 'i1', 1, 2, box.index.GE)
iterate('tweedledum', 'i1', 1, 2, box.index.GT)
iterate('tweedledum', 'i1', 1, 2, box.index.LE)
iterate('tweedledum', 'i1', 1, 2, box.index.LT)
iterate('tweedledum', 'i1', 1, 2, box.index.EQ, 'sid_005')
iterate('tweedledum', 'i1', 1, 2, box.index.REQ, 'sid_005')
iterate('tweedledum', 'i1', 1, 2, box.index.GE, 'sid_005')
iterate('tweedledum', 'i1', 1, 2, box.index.GT, 'sid_005')
iterate('tweedledum', 'i1', 1, 2, box.index.GE, 'sid_999')
iterate('tweedledum', 'i1', 1, 2, box.index.GT, 'sid_999')
iterate('tweedledum', 'i1', 1, 2, box.index.LE, 'sid_005')
iterate('tweedledum', 'i1', 1, 2, box.index.LT, 'sid_005')
iterate('tweedledum', 'i1', 1, 2, box.index.LE, 'sid_000')
iterate('tweedledum', 'i1', 1, 2, box.index.LT, 'sid_000')

-------------------------------------------------------------------------------
-- Iterator: tree multi-part unique
-------------------------------------------------------------------------------

iterate('tweedledum', 'i2', 1, 3, box.index.ALL)
iterate('tweedledum', 'i2', 1, 3, box.index.EQ)
iterate('tweedledum', 'i2', 1, 3, box.index.REQ)
iterate('tweedledum', 'i2', 1, 3, box.index.GE)
iterate('tweedledum', 'i2', 1, 3, box.index.GT)
iterate('tweedledum', 'i2', 1, 3, box.index.LE)
iterate('tweedledum', 'i2', 1, 3, box.index.LT)
iterate('tweedledum', 'i2', 1, 3, box.index.EQ, 'sid_005')
iterate('tweedledum', 'i2', 1, 3, box.index.EQ, 'sid_005', 'tid_995')
iterate('tweedledum', 'i2', 1, 3, box.index.EQ, 'sid_005', 'tid_999')
iterate('tweedledum', 'i2', 1, 3, box.index.REQ, 'sid_005')
iterate('tweedledum', 'i2', 1, 3, box.index.REQ, 'sid_005', 'tid_995')
iterate('tweedledum', 'i2', 1, 3, box.index.REQ, 'sid_005', 'tid_999')
iterate('tweedledum', 'i2', 1, 3, box.index.GE, 'sid_005')
iterate('tweedledum', 'i2', 1, 3, box.index.GT, 'sid_005')
iterate('tweedledum', 'i2', 1, 3, box.index.GE, 'sid_005', 'tid_995')
iterate('tweedledum', 'i2', 1, 3, box.index.GT, 'sid_005', 'tid_995')
iterate('tweedledum', 'i2', 1, 3, box.index.GE, 'sid_005', 'tid_999')
iterate('tweedledum', 'i2', 1, 3, box.index.GT, 'sid_005', 'tid_999')
iterate('tweedledum', 'i2', 1, 3, box.index.GE, 'sid_999')
iterate('tweedledum', 'i2', 1, 3, box.index.GT, 'sid_999')
iterate('tweedledum', 'i2', 1, 3, box.index.LE, 'sid_005')
iterate('tweedledum', 'i2', 1, 3, box.index.LT, 'sid_005')
iterate('tweedledum', 'i2', 1, 3, box.index.LE, 'sid_005', 'tid_997')
iterate('tweedledum', 'i2', 1, 3, box.index.LT, 'sid_005', 'tid_997')
iterate('tweedledum', 'i2', 1, 3, box.index.LE, 'sid_005', 'tid_000')
iterate('tweedledum', 'i2', 1, 3, box.index.LT, 'sid_005', 'tid_000')
iterate('tweedledum', 'i2', 1, 3, box.index.LE, 'sid_000')
iterate('tweedledum', 'i2', 1, 3, box.index.LT, 'sid_000')

-------------------------------------------------------------------------------
-- Iterator: tree multi-part non-unique
-------------------------------------------------------------------------------

iterate('tweedledum', 'i3', 2, 4, box.index.ALL)
iterate('tweedledum', 'i3', 2, 4, box.index.EQ)
iterate('tweedledum', 'i3', 2, 4, box.index.REQ)
iterate('tweedledum', 'i3', 2, 4, box.index.GE)
iterate('tweedledum', 'i3', 2, 4, box.index.GT)
iterate('tweedledum', 'i3', 2, 4, box.index.LE)
iterate('tweedledum', 'i3', 2, 4, box.index.LT)
iterate('tweedledum', 'i3', 2, 4, box.index.EQ, 'tid_996')
iterate('tweedledum', 'i3', 2, 4, box.index.EQ, 'tid_996', 'a')
iterate('tweedledum', 'i3', 2, 4, box.index.EQ, 'tid_996', 'z')
iterate('tweedledum', 'i3', 2, 4, box.index.REQ, 'tid_996')
iterate('tweedledum', 'i3', 2, 4, box.index.REQ, 'tid_996', 'a')
iterate('tweedledum', 'i3', 2, 4, box.index.REQ, 'tid_996', '0')
iterate('tweedledum', 'i3', 2, 4, box.index.GE, 'tid_997')
iterate('tweedledum', 'i3', 2, 4, box.index.GT, 'tid_997')
iterate('tweedledum', 'i3', 2, 4, box.index.GE, 'tid_998')
iterate('tweedledum', 'i3', 2, 4, box.index.GT, 'tid_998')
iterate('tweedledum', 'i3', 2, 4, box.index.LE, 'tid_997')
iterate('tweedledum', 'i3', 2, 4, box.index.LT, 'tid_997')
iterate('tweedledum', 'i3', 2, 4, box.index.LE, 'tid_000')
iterate('tweedledum', 'i3', 2, 4, box.index.LT, 'tid_000')
iterate('tweedledum', 'i3', 2, 4, box.index.LT, 'tid_996', 'to', 'many', 'keys')

-------------------------------------------------------------------------------
-- Iterator: various
-------------------------------------------------------------------------------

space.index['primary']:pairs({}, {iterator = 666 })
-- Test cases for #123: box.index.count does not check arguments properly
status, msg = pcall(function() space.index['primary']:pairs(function() end, { iterator = box.index.EQ }) end)
msg:match('function')

-- Check that iterators successfully invalidated when index deleted
gen, param, state = space.index['i1']:pairs(nil, { iterator = box.index.GE })
index_space = box.space[box.schema.INDEX_ID]
_ = index_space:delete{space.id, space.index['i1'].id}
type(_)
_, value = gen(param, state)
value

space:drop()

-- gh-1801 space:pairs() don't pass arguments to index:pairs()
space = box.schema.space.create('test')
pk = space:create_index('primary')
space:replace({1})
space:replace({2})
space:replace({3})
space:replace({4})
space:pairs(2, { iterator = 'GE' }):totable()
space:drop()

inspector:cmd("clear filter")


--
-- gh-1875 Add support for index:pairs(key, iterator-type) syntax
--
space = box.schema.space.create('test', {engine=engine})
pk = space:create_index('pk')
space:auto_increment{1}
space:auto_increment{2}
space:auto_increment{3}
space:auto_increment{4}
space:auto_increment{5}

--
-- test pairs()
--

space:pairs(3, 'GE'):totable()
pk:pairs(3, 'GE'):totable()
space:pairs(3, {iterator = 'GE' }):totable()
pk:pairs(3, {iterator = 'GE' }):totable()

space:pairs(3, 'EQ'):totable()
pk:pairs(3, 'EQ'):totable()
space:pairs(3, {iterator = 'EQ' }):totable()
pk:pairs(3, {iterator = 'EQ' }):totable()

space:pairs(3, 'GT'):totable()
pk:pairs(3, 'GT'):totable()
space:pairs(3, {iterator = 'GT' }):totable()
pk:pairs(3, {iterator = 'GT' }):totable()

--
-- test select()
--

pk:select({3}, 'LE')
space:select({3}, 'LE')

--
-- test count()
--

pk:count({3}, 'GT')
space:count({3}, 'GT')

space:drop()

-- vinyl: broken rollback to savepoint
-- https://github.com/tarantool/tarantool/issues/2589
s = box.schema.create_space('s', { engine = engine})
i1 = s:create_index('i1', { type = 'tree', parts = {1,'unsigned'}, unique = true })
i2 = s:create_index('i2', { type = 'tree', parts = {2,'unsigned'}, unique = true })

_ = s:replace{2, 2}

box.begin()
_ = s:replace{1, 1}
_ = pcall(s.upsert, s, {1, 1}, {{"+", 2, 1}}) -- failed in unique secondary
box.commit()

s:select{}
s:drop{}

-- implement lazy iterator positioning
s = box.schema.space.create('test' ,{engine=engine})
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for i = 1,3 do for j = 1,3 do s:replace{i, j} end end

itr1,itr2,itr3 = s:pairs{2}
_ = s:replace{1, 4}
r = {}
for k,v in itr1,itr2,itr3 do table.insert(r, v) end
r

itr1,itr2,itr3 = s:pairs({2}, {iterator = 'GE'})
_ = s:replace{1, 5}
r = {}
for k,v in itr1,itr2,itr3 do table.insert(r, v) end
r

itr1,itr2,itr3 = s:pairs({2}, {iterator = 'REQ'})
s:replace{2, 4}
r = {}
for k,v in itr1,itr2,itr3 do table.insert(r, v) end
r

r = nil
s:drop()

-- make tree iterators stable
-- https://github.com/tarantool/tarantool/issues/1796
s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })

for i = 1,10 do s:replace{i} end
r = {}
for k,v in s:pairs{} do table.insert(r, v[1]) s:delete(v[1]) end
r
s:select{}

for i = 1,10 do s:replace{i} end
r = {}
for k,v in s:pairs({}, {iterator = 'REQ'}) do table.insert(r, v[1]) s:delete(v[1]) end
r
s:select{}

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })

for i = 1,3 do for j = 1,3 do s:replace{i, j} end end
r = {}
for k,v in s:pairs{2} do table.insert(r, v) s:delete{v[1], v[2]} end
r
s:select{}

for i = 1,3 do for j = 1,3 do s:replace{i, j} end end
r = {}
for k,v in s:pairs({3}, {iterator = 'REQ'}) do table.insert(r, v) s:delete{v[1], v[2]} end
r
s:select{}

r = nil
s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({25})
s:replace{25}
state, value = gen(param,state)
value
state, value = gen(param,state)
value

gen,param,state = i:pairs({35})
state, value = gen(param,state)
value
s:replace{35}
state, value = gen(param,state)
value

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({30}, {iterator = 'GE'})
state, value = gen(param, state)
value
s:replace{0}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
state, value = gen(param, state)
value
state, value = gen(param, state)
state
value

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({40}, {iterator = 'LE'})
state, value = gen(param, state)
value
s:replace{0}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{32}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
state, value = gen(param, state)
state
value

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({28}, {iterator = 'GE'})
s:replace{0}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{32}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
state, value = gen(param, state)
value
gen(param, state)
-- test iterator dummy function, invoked when it's out of bounds
gen(param, state)

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({42}, {iterator = 'LE'})
s:replace{0}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
s:replace{32}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
state, value = gen(param, state)
value
gen(param, state)
-- test iterator dummy function, invoked when it's out of bounds
gen(param, state)

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({20}, {iterator = 'GT'})
state, value = gen(param, state)
value
s:replace{0}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
state, value = gen(param, state)
value
gen(param, state)
-- test iterator dummy function, invoked when it's out of bounds
gen(param, state)

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({50}, {iterator = 'LT'})
state, value = gen(param, state)
value
s:replace{0}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{32}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
gen(param, state)
-- test iterator dummy function, invoked when it's out of bounds
gen(param, state)

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({28}, {iterator = 'GT'})
s:replace{0}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{32}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
state, value = gen(param, state)
value
gen(param, state)
-- test iterator dummy function, invoked when it's out of bounds
gen(param, state)

s:drop()

s = box.schema.space.create('test')
i = s:create_index('i', { type = 'tree', parts = {1, 'unsigned'} })
s:replace{10} s:replace{20} s:replace{30} s:replace{40} s:replace{50} s:replace{60}

gen,param,state = i:pairs({42}, {iterator = 'LT'})
s:replace{0}
state, value = gen(param, state)
value
s:replace{42}
state, value = gen(param, state)
value
s:replace{15}
state, value = gen(param, state)
value
s:replace{32}
state, value = gen(param, state)
value
s:replace{80}
state, value = gen(param, state)
value
state, value = gen(param, state)
value
gen(param, state)
-- test iterator dummy function, invoked when it's out of bounds
gen(param, state)

s:drop()
