iterate = dofile('utils.lua').iterate

test_run = require('test_run').new()
test_run:cmd("push filter '(error: .builtin/.*[.]lua):[0-9]+' to '\\1'")
# Tree single-part unique

space = box.schema.space.create('tweedledum')
idx1 = space:create_index('primary', { type = 'tree', parts = {1, 'string'}, unique = true})
-- Hash single-part unique
idx5 = space:create_index('i4', { type = 'hash', parts = {1, 'string'}, unique = true})
-- Hash multi-part unique
idx6 = space:create_index('i5', { type = 'hash', parts = {2, 'string', 3, 'string'}, unique = true})

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
iterate = nil
