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

engine = nil
test_run = nil
