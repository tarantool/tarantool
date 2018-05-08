test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
test_run:cmd("push filter 'Failed to allocate [0-9]+' to 'Failed to allocate <NUM>'")
test_run:cmd("push filter '"..engine.."_max_tuple_size' to '<ENGINE>_max_tuple_size'")

-- https://github.com/tarantool/tarantool/issues/2667
-- Allow to insert tuples bigger than `max_tuple_size'
s = box.schema.space.create('test', { engine = engine })
_ = s:create_index('primary')

engine_max_tuple_size = engine ..'_max_tuple_size'
engine_tuple_size = engine == 'memtx' and 16 or 32
box.cfg{[engine_max_tuple_size] = 1024 * 1024}

-- check max_tuple_size limit
max_tuple_size = box.cfg[engine_max_tuple_size]
_ = s:replace({1, string.rep('x', max_tuple_size)})

-- check max_tuple_size dynamic configuration
box.cfg { [engine_max_tuple_size] = 2 * max_tuple_size }
_ = s:replace({1, string.rep('x', max_tuple_size)})

-- check tuple sie
box.cfg { [engine_max_tuple_size] = engine_tuple_size + 2 }
_ = s:replace({1})

-- check large tuples allocated on malloc
box.cfg { [engine_max_tuple_size] = 32 * 1024 * 1024 }
_ = s:replace({1, string.rep('x', 32 * 1024 * 1024 - engine_tuple_size - 8)})

-- decrease max_tuple_size limit
box.cfg { [engine_max_tuple_size] = 1 * 1024 * 1024 }
_ = s:replace({1, string.rep('x', 2 * 1024 * 1024 )})
_ = s:replace({1, string.rep('x', 1 * 1024 * 1024 - engine_tuple_size - 8)})

-- gh-2698 Tarantool crashed on 4M tuple
max_item_size = 0
test_run:cmd("setopt delimiter ';'")
for _, v in pairs(box.slab.stats()) do
    max_item_size = math.max(max_item_size, v.item_size)
end;
test_run:cmd("setopt delimiter ''");
box.cfg { [engine_max_tuple_size] = max_item_size + engine_tuple_size + 8 }
_ = box.space.test:replace{1, 1, string.rep('a', max_item_size)}

-- reset to original value
box.cfg { [engine_max_tuple_size] = max_tuple_size }

s:drop();
collectgarbage('collect') -- collect all large tuples
box.snapshot() -- discard xlogs with large tuples
test_run:cmd("clear filter")

--
-- gh-1014: tuple field names.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'string'}
format[3] = {name = 'field3', type = 'array'}
format[4] = {name = 'field4', type = 'number'}
format[5] = {name = 'field5', type = 'integer'}
format[6] = {name = 'field6', type = 'scalar'}
format[7] = {name = 'field7', type = 'map'}
format[8] = {name = 'field8', type = 'any'}
format[9] = {name = 'field9'}
format[10] = {name = 'bsize'}
format[11] = {name = 'totable'}
format[12] = {name = 'longlonglonglonglonglongname'}
s = box.schema.space.create('test', {engine = engine, format = format})
pk = s:create_index('pk')
t = {1, '2', {3, 3}, 4.4, -5, true, {key = 7}, 8, 9, 10, 11, 12}
t = s:replace(t)
t
t.field1, t.field2, t.field3, t.field4, t.field5, t.field6, t.field7, t.field8, t.field9, t.bsize, t.totable
t.longlonglonglonglonglongname
box.tuple.bsize(t)
box.tuple.totable(t)
s:drop()

--
-- Increase collisions number and make juajit use second hash
-- function.
--
format = {}
for i = 1, 100 do format[i] = {name = "skwjhfjwhfwfhwkhfwkjh"..i.."avjnbknwkvbwekjf"} end
s = box.schema.space.create('test', { engine = engine, format = format })
p = s:create_index('pk')
to_insert = {}
for i = 1, 100 do to_insert[i] = i end
t = s:replace(to_insert)
format = nil
name = nil
s = nil
p = nil
to_insert = nil
collectgarbage('collect')
-- Print many many strings (> 40 to reach max_collisions limit in luajit).
t.skwjhfjwhfwfhwkhfwkjh01avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh02avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh03avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh04avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh05avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh06avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh07avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh08avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh09avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh10avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh11avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh12avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh13avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh14avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh15avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh16avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh17avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh18avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh19avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh20avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh21avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh22avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh23avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh24avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh25avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh26avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh27avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh28avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh29avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh30avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh31avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh32avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh33avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh34avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh35avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh36avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh37avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh38avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh39avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh40avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh41avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh42avjnbknwkvbwekjf
t.skwjhfjwhfwfhwkhfwkjh43avjnbknwkvbwekjf

box.space.test:drop()

--
-- gh-2773: correctly reset max tuple size on restart.
--
box.cfg{[engine_max_tuple_size] = 1024 * 1024 * 100}
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk')
_ = s:replace({1, string.rep('*', 1024 * 1024)})
_ = s:replace({2, string.rep('*', 1024 * 1024 * 2)})
pk:count()
test_run:cmd('restart server default')
engine = test_run:get_cfg('engine')
s = box.space.test
s:count()
s:drop()

--
-- gh-2821: tuple:tomap().
--
format = {}
format[1] = {'field1', 'unsigned'}
format[2] = {'field2', 'unsigned'}
format[3] = {'field3', 'unsigned'}
format[4] = {'field4', 'array'}
s = box.schema.space.create('test', {format = format, engine = engine})
pk = s:create_index('pk')
t1 = s:replace{1, 2, 3, {'a', 'b', 'c'}}
t1map = t1:tomap()
function maplen(map) local count = 0 for _ in pairs(map) do count = count + 1 end return count end
maplen(t1map), t1map.field1, t1map.field2, t1map.field3, t1map.field4
t1map[1], t1map[2], t1map[3], t1map[4]
-- Fields with table type are stored once for name and for index.
t1map[4] == t1map.field4

t2 = s:replace{4, 5, 6, {'a', 'b', 'c'}, 'extra1'}
t2map = t2:tomap()
maplen(t2map), t2map.field1, t2map.field2, t2map.field3, t2map.field4
t1map[1], t1map[2], t1map[3], t2map[4], t2map[5]

-- Use box.tuple.tomap alias.
t3 = s:replace{7, 8, 9, {'a', 'b', 'c'}, 'extra1', 'extra2'}
t3map = box.tuple.tomap(t3)
maplen(t3map), t3map.field1, t3map.field2, t3map.field3, t3map.field4
t1map[1], t1map[2], t1map[3], t3map[4], t3map[5], t3map[6]

-- Invalid arguments.
t3.tomap('123')
box.tuple.tomap('456')

s:drop()

-- No names, no format.
s = box.schema.space.create('test', { engine = engine })
pk = s:create_index('pk')
t1 = s:replace{1, 2, 3}
t1map = t1:tomap()
maplen(t1map), t1map[1], t1map[2], t1map[3]
s:drop()

--
-- gh-2821: tuple:tomap() names_only feature.
--
format = {}
format[1] = {name = 'field1', type = 'unsigned' }
format[2] = {name = 'field2', type = 'unsigned' }
s = box.schema.create_space('test', {format = format})
pk = s:create_index('pk')
t = s:replace{100, 200, 300 }
t:tomap({names_only = false})
t:tomap({names_only = true})
t:tomap({names_only = 'text'})
t:tomap({names_only = true}, {dummy = true})
t:tomap({})
s:drop()
s = box.schema.create_space('test')
pk = s:create_index('pk')
t = s:replace{1,2,3,4,5,6,7}
t:tomap({names_only = true})
s:drop()

format = {}
format[1] = {name = 'field1', type = 'unsigned'}
format[2] = {name = 'field2', type = 'array'}
format[3] = {name = 'field3', type = 'map'}
format[4] = {name = 'field4', type = 'string' }
format[5] = {name = "[2][6]['привет中国world']['中国a']", type = 'string'}
format[6] = {name = '[1]', type = 'any'}
s = box.schema.space.create('test', {format = format})
pk = s:create_index('pk')
field2 = {1, 2, 3, "4", {5,6,7}, {привет中国world={中国="привет"}, key="value1", value="key1"}}
field3 = {[10] = 100, k1 = 100, k2 = {1,2,3}, k3 = { {a=1, b=2}, {c=3, d=4} }, [-1] = 200}
t = s:replace{1, field2, field3, "123456", "yes, this", {key = 100}}
t[1]
t[2]
t[3]
t[4]
t[2][1]
t["[2][1]"]
t[2][5]
t["[2][5]"]
t["[2][5][1]"]
t["[2][5][2]"]
t["[2][5][3]"]
t["[2][6].key"]
t["[2][6].value"]
t["[2][6]['key']"]
t["[2][6]['value']"]
t[2][6].привет中国world.中国
t["[2][6].привет中国world"].中国
t["[2][6].привет中国world.中国"]
t["[2][6]['привет中国world']"]["中国"]
t["[2][6]['привет中国world']['中国']"]
t["[2][6]['привет中国world']['中国a']"]
t["[3].k3[2].c"]
t["[4]"]
t.field1
t.field2[5]
t[".field1"]
t["field1"]
t["[3][10]"]
t["[1]"]
t["['[1]'].key"]

-- Not found.
t[0]
t["[0]"]
t["[1000]"]
t.field1000
t["not_found"]
t["[2][5][10]"]
t["[2][6].key100"]
t["[2][0]"] -- 0-based index in array.
t["[4][3]"] -- Can not index string.
t["[4]['key']"]
-- Not found 'a'. Return 'null' despite of syntax error on a
-- next position.
t["a.b.c d.e.f"]

-- Sytax errors.
t["[2].[5]"]
t["[-1]"]
t[".."]
t["[["]
t["]]"]
t["{"]

s:drop()

engine = nil
test_run = nil
