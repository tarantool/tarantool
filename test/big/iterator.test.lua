dofile('utils.lua')

test_run = require('test_run').new()
test_run:cmd("push filter '(error: .builtin/.*[.]lua):[0-9]+' to '\\1'")
# Tree single-part unique

space = box.schema.space.create('tweedledum')
idx1 = space:create_index('primary', { type = 'tree', parts = {1, 'str'}, unique = true})
-- Tree single-part non-unique
idx2 = space:create_index('i1', { type = 'tree', parts = {2, 'str'}, unique = false})
-- Tree multi-part unique
idx3 = space:create_index('i2', { type = 'tree', parts = {2, 'str', 3, 'str'}, unique = true})
-- Tree multi-part non-unique
idx4 = space:create_index('i3', { type = 'tree', parts = {3, 'str', 4, 'str'}, unique = false })
-- Hash single-part unique
idx5 = space:create_index('i4', { type = 'hash', parts = {1, 'str'}, unique = true})
-- Hash multi-part unique
idx6 = space:create_index('i5', { type = 'hash', parts = {2, 'str', 3, 'str'}, unique = true})

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
-- Iterator: hash single-part unique
-------------------------------------------------------------------------------

iterate('tweedledum', 'i4', 0, 1)
iterate('tweedledum', 'i4', 0, 1, box.index.ALL)
iterate('tweedledum', 'i4', 0, 1, box.index.EQ)
iterate('tweedledum', 'i4', 0, 1, box.index.EQ, 'pid_003')
iterate('tweedledum', 'i4', 0, 1, box.index.EQ, 'pid_666')

-------------------------------------------------------------------------------
-- Iterator: hash multi-part unique
-------------------------------------------------------------------------------

iterate('tweedledum', 'i5', 1, 3, box.index.ALL)
iterate('tweedledum', 'i5', 1, 3, box.index.EQ, 'sid_005')
iterate('tweedledum', 'i5', 1, 3, box.index.EQ, 'sid_005', 'tid_995')
iterate('tweedledum', 'i5', 1, 3, box.index.EQ, 'sid_005', 'tid_999')
iterate('tweedledum', 'i5', 1, 3, box.index.EQ, 'sid_005', 'tid_995', 'a')

-------------------------------------------------------------------------------
-- Iterator: various
-------------------------------------------------------------------------------

space.index['primary']:pairs({}, {iterator = -666 })
-- Test cases for #123: box.index.count does not check arguments properly
space.index['primary']:pairs(function() end, { iterator = box.index.EQ })

-- Check that iterators successfully invalidated when index deleted
gen, param, state = space.index['i1']:pairs(nil, { iterator = box.index.GE })
index_space = box.space[box.schema.INDEX_ID]
_ = index_space:delete{space.id, space.index['i1'].id}
type(_)
gen(param, state)

space:drop()

-------------------------------------------------------------------------------
-- Iterator: https://github.com/tarantool/tarantool/issues/464
-- Iterator safety after changing schema
-------------------------------------------------------------------------------

space = box.schema.space.create('test', {temporary=true})
idx1 = space:create_index('primary', {type='HASH',unique=true})
idx2 = space:create_index('t1', {type='TREE',unique=true})
idx3 = space:create_index('t2', {type='TREE',unique=true})

box.space.test:insert{0}
box.space.test:insert{1}

gen1, param1, state1 = space.index.t1:pairs({}, {iterator = box.index.ALL})
gen1(param1, state1)

gen2, param2, state2 = space.index.t2:pairs({}, {iterator = box.index.ALL})
gen2(param2, state2)

id = space.index.t1.id
box.schema.index.drop(space.id, id)

gen1(param1, state1)
gen2(param2, state2)

gen2, param2, state2 = space.index.t2:pairs({}, {iterator = box.index.ALL})
gen2(param2, state2)
gen2(param2, state2)

space:drop()

-------------------------------------------------------------------------------
-- Iterator: https://github.com/tarantool/tarantool/issues/498
-- Iterator is not checked for wrong type; accept lowercase iterator
-------------------------------------------------------------------------------

space = box.schema.space.create('test', {temporary=true})
idx1 = space:create_index('primary', {type='TREE',unique=true})
space:insert{0}
space:insert{1}

gen, param, state = space.index.primary:pairs({}, {iterator = 'ALL'})
gen(param, state)
gen(param, state)
gen(param, state)

gen, param, state = space.index.primary:pairs({}, {iterator = 'all'})
gen(param, state)
gen(param, state)

gen, param, state = space.index.primary:pairs({}, {iterator = 'mistake'})

space:select({}, {iterator = box.index.ALL})
space:select({}, {iterator = 'all'})
space:select({}, {iterator = 'mistake'})

space:drop()


-------------------------------------------------------------------------------
--  Restore GE iterator for HASH https://github.com/tarantool/tarantool/issues/836
-------------------------------------------------------------------------------
space = box.schema.space.create('test', {temporary=true})
idx1 = space:create_index('primary', {type='hash',unique=true})

for i = 0,5 do space:insert{i} end

space:select(2)
space:select(5, {iterator="GE"})
space:select(nil, {iterator="GE"})
space:select(5, {iterator="GT"})
l = space:select(nil, {limit=2, iterator="GT"})
l
l = space:select(l[#l][1], {limit=2, iterator="GT"})
l
l = space:select(l[#l][1], {limit=2, iterator="GT"})
l
l = space:select(l[#l][1], {limit=2, iterator="GT"})
l
space:drop()
