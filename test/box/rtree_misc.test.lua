env = require('test_run')
test_run = env.new()

s = box.schema.space.create('spatial')

-- rtree index as primary key must be forbidden (unique)
i = s:create_index('spatial', { type = 'rtree', unique = true, parts = {1, 'array'}})

-- any non-unique index as primary key must be forbidden
i = s:create_index('spatial', { type = 'hash', unique = false, parts = {1, 'unsigned'}})
i = s:create_index('spatial', { type = 'tree', unique = false, parts = {1, 'unsigned'}})
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {1, 'array'}})

-- tree and hash indexes over array field is not possible
i = s:create_index('primary', { type = 'tree', parts = {1, 'array'}})
i = s:create_index('primary', { type = 'hash', parts = {1, 'array'}})

-- normal indexes
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
i = s:create_index('secondary', { type = 'hash', parts = {2, 'unsigned'}})

-- adding a tuple with array instead of num will fail
i = s:insert{{1, 2, 3}, 4}
i = s:insert{1, {2, 3, 4}}

-- rtree index must be one-part
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {1, 'array', 2, 'array'}})

-- unique rtree index is not possible
i = s:create_index('spatial', { type = 'rtree', unique = true, parts = {3, 'array'}})

-- num rtree index is not possible
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {3, 'unsigned'}})

-- str rtree index is not possible
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {3, 'string'}})


-- normal rtree index
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {3, 'array'}})

-- inserting wrong values (should fail)
s:insert{1, 2, 3}
s:insert{1, 2, "3"}
s:insert{1, 2, nil, 3}
s:insert{1, 2, {}}
s:insert{1, 2, {"3", "4", "5", "6"}}
s:insert{1, 2, {nil, 4, 5, 6}}
s:insert{1, 2, {3, {4}, 5, 6}}
s:insert{1, 2, {3, 4, {}, 6}}
s:insert{1, 2, {3, 4, 5, "6"}}
s:insert{1, 2, {3}}
s:insert{1, 2, {3, 4, 5}}

-- inserting good value
s:insert{1, 2, {3, 4, 5, 6}}

-- invalid alters
s.index.spatial:alter({unique = true})
s.index.spatial:alter({type = 'tree'})
box.space[box.schema.SPACE_ID]:update({s.id}, {{"=", 4, 'vinyl'}})

-- chech that truncate works
s.index.spatial:select({0, 0, 10, 10}, {iterator = 'le'})
s:truncate()
s.index.spatial:select({0, 0, 10, 10}, {iterator = 'le'})

-- inserting lots of equvalent records
for i = 1,500 do s:insert{i, i, {3, 4, 5, 6}} end
-- and some records for chaos
for i = 1,10 do for j = 1,10 do s:insert{500+i+j*20, 500+i*20+j, {i, j, i, j}} end end
s.index.spatial:count()
#s.index.spatial:select({3, 4, 5, 6})
for i = 1,500,2 do s:delete{i} end
s.index.spatial:count()
#s.index.spatial:select({3, 4, 5, 6})

s.index.spatial:min()
s.index.spatial:max()

-- seems that drop can't fail
s.index.spatial:drop()
s.index.spatial:select({})

s:drop()

s = box.schema.space.create('vinyl', {engine = 'vinyl'})
-- rtree indexes are not enabled in vinyl
i = s:create_index('spatial', { type = 'rtree', unique = true, parts = {3, 'array'}})
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
-- ... even secondary
i = s:create_index('spatial', { type = 'rtree', unique = true, parts = {3, 'array'}})
s:drop()

-- rtree in temp space must work fine
s = box.schema.space.create('spatial', {temporary = true})
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {3, 'array'}})
s:insert{1, 2, {3, 4, 5, 6}}
s.index.spatial:select({0, 0, 10, 10}, {iterator = 'le'})
s:drop()

-- snapshot test
s = box.schema.space.create('spatial')
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {3, 'array'}})
for i = 1,10 do s:insert{i, i, {i, i, i + 1, i + 1}} end
box.snapshot()
i:select({0, 0}, {iterator = 'neighbor'})

test_run:cmd("restart server default")

s = box.space.spatial
i = s.index.spatial
i:select({0, 0}, {iterator = 'neighbor'})
s:drop()

s = box.schema.space.create('spatial')
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
i = s:create_index('spatial', { type = 'rtree', unique = false, parts = {3, 'array'}, dimension = 4})
for i = 1,10 do s:insert{i, i, {i, i, i, i, i + 1, i + 1, i + 1, i + 1}} end
box.snapshot()
i:select({0, 0, 0, 0}, {iterator = 'neighbor'})

test_run:cmd("restart server default")

s = box.space.spatial
i = s.index.spatial
i:select({0, 0, 0, 0}, {iterator = 'neighbor'})
s:drop()

-- distance type
iopts = { type = 'rtree', unique = false, parts = {2, 'array'} }
iopts['distance'] = 'euclid'
s = box.schema.space.create('spatial')
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
i = s:create_index('spatial', iopts)
s:insert{1, {0, 5}}
s:insert{2, {5, 0}}
s:insert{3, {5, 5}}
s:insert{4, {8, 0}}
s:insert{5, {0, 8}}
s.index.spatial:select({{0, 0}}, {iterator = 'neighbor'})
s:drop()

iopts = { type = 'rtree', unique = false, parts = {2, 'array'} }
iopts['distance'] = 'manhattan'
s = box.schema.space.create('spatial')
i = s:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})
i = s:create_index('spatial', iopts)
s:insert{1, {0, 5}}
s:insert{2, {5, 0}}
s:insert{3, {5, 5}}
s:insert{4, {8, 0}}
s:insert{5, {0, 8}}
s.index.spatial:select({{0, 0}}, {iterator = 'neighbor'})

test_run:cmd("restart server default")

s = box.space.spatial
i = s.index.spatial
s.index.spatial:select({{0, 0}}, {iterator = 'neighbor'})
box.snapshot()

test_run:cmd("restart server default")

utils = require('utils')

s = box.space.spatial
i = s.index.spatial
s.index.spatial:select({{0, 0}}, {iterator = 'neighbor'})
s:drop()


-- RTREE QA https://github.com/tarantool/tarantool/issues/976
s = box.schema.space.create('s')
i = s:create_index('p')

-- dimension too big
i = s:create_index('s', {type = 'rtree', parts = {2, 'array'}, dimension = 21})

-- dimension too low
i = s:create_index('s', {type = 'rtree', parts = {2, 'array'}, dimension = 0})

-- cant be unique
i = s:create_index('s', {type = 'rtree', parts = {2, 'array'}, unique = true})

-- wrong parts
i = s:create_index('s', {type = 'rtree', parts = {2, 'unsigned'}})
i = s:create_index('s', {type = 'rtree', parts = {2, 'array', 3, 'array'}})

-- defaults test
i = s:create_index('s', { type = 'rtree' })
i.dimension
i.parts
i:drop()

-- hide first (id) field of tuple
function f(t) local r = {} for i, v in ipairs(t) do r[i] = v end r[1] = 0 return setmetatable (r, {__serialize = 'seq'}) end

-- new index through inserting to _index space
f(box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false}, {{2, 'array'}}})
s.index.s:drop()

-- with wrong args
box.space._index:insert{s.id, 2, 's', 'rtree', nil, {{2, 'array'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', utils.setmap({}), {{2, 'array'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false, dimension = 22}, {{2, 'array'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false, dimension = 'dimension'}, {{2, 'array'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false}, {{2, 'unsigned'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false}, {{2, 'time'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false}, {{'no','time'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false, distance = 'lobachevsky'}, {{2, 'array'}}}
box.space._index:insert{s.id, 2, 's', 'rtee', {unique = false}, {{2, 'array'}}}
box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false}, {{}}}
-- unknown args checked
f(box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false, holy = 'cow'}, {{2, 'array'}}})

-- unknown part args are no more ignored (#2649)
f(box.space._index:insert{s.id, 2, 's', 'rtree', {unique = false}, {{field=2, type='array', part = 'opts'}}})

-- alter
i = s:create_index('s', {type = 'rtree', parts = {2, 'array'}})
i:alter{type = 'tree' }
i:alter{dimension = 3 }
s:insert{1, {1, 1} }
s:insert{1, {1, 1, 1} }
i:alter{dimension = 4 }
s:select{}
s:insert{2, {2, 0, 0} }
i:alter{distance = 'euclid' }
i:select({0, 0, 0}, {iterator = 'neighbor'})
i:alter{distance = 'manhattan' }
i:select({0, 0, 0}, {iterator = 'neighbor'})

-- gh-1467: invalid iterator type
i:select({1, 2, 3, 4, 5, 6}, {iterator = 'BITS_ALL_SET' } )

s:drop()

-- Rtree index cannot be multikey.
s = box.schema.space.create('test')
_ = s:create_index('primary')
_ = s:create_index('rtree', {type = 'rtree', parts = {{'[2][*]', 'array'}}})
s:drop()

-- Rtree index can not use function.
s = box.schema.space.create('withdata')
lua_code = [[function(tuple) return {tuple[1] + tuple[2], tuple[1] - tuple[2]} end]]
box.schema.func.create('fextract', {body = lua_code, is_deterministic = true, is_sandboxed = true})
_ = s:create_index('pk')
_ = s:create_index('idx', {type = 'rtree', func = box.func.fextract.id, parts = {{1, 'array'}}})
s:drop()
box.schema.func.drop('fextract')
