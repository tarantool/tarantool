
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

-- upsert (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:upsert({tostring(key), 0}, {{'+', 2, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:upsert({tostring(key), 0}, {{'+', 2, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
for key = 1, 100 do space:delete({tostring(key)}) end
for key = 1, 100 do space:upsert({tostring(key), 0}, {{'+', 2, 1}, {'=', 3, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:drop()


-- upsert (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:upsert({key, 0}, {{'+', 2, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:upsert({key, 0}, {{'+', 2, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
for key = 1, 100 do space:delete({key}) end
for key = 1, 100 do space:upsert({key, 0}, {{'+', 2, 1}, {'=', 3, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:drop()


-- upsert multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:upsert({key, key, 0}, {{'+', 3, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:upsert({key, key, 0}, {{'+', 3, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:delete({key, key}) end
for key = 1, 100 do space:upsert({key, key, 0}, {{'+', 3, 1}, {'=', 4, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:drop()

test_run:cmd("setopt delimiter ';'");
function less(a, b)
    if type(a[2]) ~= type(b[2]) then
        return type(a[2]) < type(b[2])
    end
    if a[2] == b[2] then
        return a[1] < b[1]
    end
    if type(a[2]) == 'boolean' then
        return a[2] == false and b[2] == true
    end
    return a[2] < b[2]
end;
test_run:cmd("setopt delimiter ''");
function sort(t) table.sort(t, less) return t end


-- upsert default tuple constraint
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
space:upsert({0, 'key', 0}, {{'+', 3, 1}})
space:drop()


-- upsert primary key modify (skipped)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space:upsert({0, 0}, {{'+', 1, 1}, {'+', 2, 1}})
space:get({0})
space:drop()

-- upsert with box.tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:upsert(box.tuple.new{key, key, 0}, box.tuple.new{{'+', 3, 1}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:upsert(box.tuple.new{key, key, 0}, box.tuple.new{{'+', 3, 10}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
for key = 1, 100 do space:delete({key, key}) end
for key = 1, 100 do space:upsert(box.tuple.new{key, key, 0}, box.tuple.new{{'+', 3, 1}, {'=', 4, key}}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:drop()

-- https://github.com/tarantool/tarantool/issues/1671

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'unsigned'}, unique = false })
space:insert({1, 1})
space:insert({2, 2})
index1:select{}
index2:select{}
space:upsert({1, 1}, {{'=', 2, 2}})
sort(index1:select{})
sort(index2:select{})
space:drop()

s = box.schema.space.create('tweedledum', { engine = engine })
index = s:create_index('pk')
s:upsert({0, 0}, {{'+', 2, 2}})
s:select{0}
tmp = s:delete{0}
s:upsert({0, 0, 0}, {{'+', 2, 2}})
s:select{0}
tmp = s:delete{0}
s:upsert({0}, {{'+', 2, 2}})
s:select{0}
s:replace{0, 1, 2, 4}
s:upsert({0, 0, "you will not see it"}, {{'+', 2, 2}})
s:select{0}
s:replace{0, -0x4000000000000000ll}
s:upsert({0}, {{'+', 2, -0x4000000000000001ll}})  -- overflow
s:select{0}
s:replace{0, "thing"}
s:upsert({0, "nothing"}, {{'+', 2, 2}})
s:select{0}
tmp = s:delete{0}
s:upsert({0, "thing"}, {{'+', 2, 2}})
s:select{0}
s:replace{0, 1.5}
s:select{}
s:upsert({0}, {{'|', 1, 255}})
s:select{0}
s:replace{0, 1.5}
s:replace{0, 'something to splice'}
s:upsert({0}, {{':', 2, 1, 4, 'no'}})
s:select{0}
s:upsert({0}, {{':', 2, 1, 2, 'every'}})
s:select{0}
s:upsert({0}, {{':', 2, -100, 2, 'every'}})
s:select{0}
s:drop()


space = box.schema.space.create('test', { engine = engine, field_count = 1 })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space:insert({1})
space:select{}
space:upsert({2, 2}, {{'+', 2, 1}})
-- TODO: https://github.com/tarantool/tarantool/issues/1622
-- space:upsert({1}, {{'=', 2, 10}})
space:select{}
space:drop()

space = box.schema.space.create('test', { engine = engine, field_count = 2 })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space:insert({1, 1})
space:select{}
space:upsert({2, 2, 2}, {{'+', 3, 1}})
space:upsert({3, 3}, {{'+', 2, 1}})
-- TODO: https://github.com/tarantool/tarantool/issues/1622
--space:upsert({4}, {{'=', 2, 10}})
--space:upsert({1}, {{'#', 2}})
space:select{}
space:drop()

--UPSERT https://github.com/tarantool/tarantool/issues/966
test_run:cmd("setopt delimiter ';'")
function anything_to_string(tab)
    if tab == nil then
        return 'nil'
    end
    local str = '['
    local first_route = true
    local t = 0
    for k,f in pairs(tab) do
        if not first_route then str = str .. ',' end
        first_route = false
        t = t + 1
        if k ~= t then
            str = str .. k .. '='
        end
        if type(f) == 'string' then
            str = str .. "'" .. f .. "'"
        elseif type (f) == 'number' then
            str = str .. tostring(f)
        elseif type (f) == 'table' or type (f) == 'cdata' then
            str = str .. anything_to_string(f)
        else
            str = str .. '?'
        end
    end
    str = str .. ']'
    return str
end;

function things_equal(var1, var2)
    local type1 = type(var1) == 'cdata' and 'table' or type(var1)
    local type2 = type(var2) == 'cdata' and 'table' or type(var2)
    if type1 ~= type2 then
        return false
    end
    if type1 ~= 'table' then
        return var1 == var2
    end
    for k,v in pairs(var1) do
        if not things_equal(v, var2[k]) then
            return false
        end
    end
    for k,v in pairs(var2) do
        if not things_equal(v, var1[k]) then
            return false
        end
    end
    return true
end;

function copy_thing(t)
    if type(t) ~= 'table' then
        return t
    end
    local res = {}
    for k,v in pairs(t) do
        res[copy_thing(k)] = copy_thing(v)
    end
    return res
end;

function test(key_tuple, ops, expect)
    box.space.s:upsert(key_tuple, ops)
    if (things_equal(box.space.s:select{}, expect)) then
        return 'upsert('.. anything_to_string(key_tuple) .. ', ' ..
                anything_to_string(ops) .. ', '  ..
                ') OK ' .. anything_to_string(box.space.s:select{})
    end
    return 'upsert('.. anything_to_string(key_tuple) .. ', ' ..
            anything_to_string(ops) .. ', ' ..
            ') FAILED, got ' .. anything_to_string(box.space.s:select{}) ..
            ' expected ' .. anything_to_string(expect)
end;
test_run:cmd("setopt delimiter ''");

-- https://github.com/tarantool/tarantool/issues/1671
-- test upserts without triggers

-- test case with one index

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { parts = {1, 'string'} })
space:upsert({1}, {{'!', 2, 100}}) -- must fail on checking tuple
space:upsert({'a'}, {{'a', 2, 100}}) -- must fail on checking ops
space:upsert({'a'}, {{'!', 2, 'ups1'}}) -- 'fast' upsert via insert in one index
space:upsert({'a', 'b'}, {{'!', 2, 'ups2'}}) -- 'fast' upsert via update in one index
space:select{}
space:drop()

-- tests on multiple indexes

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { parts = {1, 'string'} })
index2 = space:create_index('secondary', { parts = {2, 'scalar', 3, 'unsigned'} })
-- test upsert that executes as insert in all indexes
space:upsert({'a', 100, 100}, {{'!', 4, 200}})
space:upsert({'b', 100, 200}, {{'!', 4, 300}})
space:upsert({'c', 100, 300}, {{'!', 4, 400}})
index1:select{}
index2:select{}

-- test upsert that executes as update
space:upsert({'a', 100, 100}, {{'=', 3, -200}}) -- must fail on cheking new tuple in secondary index
space:upsert({'b', 100, 200}, {{'=', 1, 'd'}}) -- must fail with attempt to modify primary index
index1:select{}
index2:select{}

space:drop()

-- https://github.com/tarantool/tarantool/issues/1854
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:upsert({2, 18, 76}, {})
space:upsert({4, 4, 4}, {})
space:select{}
space:drop()

s = box.schema.space.create('s', { engine = engine })
index1 = s:create_index('i1')
index2 = s:create_index('i2', { parts = {2, 'string'}, unique = false })

t = {1, '1', 1, 'qwerty'}
s:insert(t)

-- all good operations, one op, equivalent to update
test(t, {{'+', 3, 5}}, {{1, '1', 6, 'qwerty'}})
test(t, {{'-', 3, 3}}, {{1, '1', 3, 'qwerty'}})
test(t, {{'&', 3, 5}}, {{1, '1', 1, 'qwerty'}})
test(t, {{'|', 3, 8}}, {{1, '1', 9, 'qwerty'}})
test(t, {{'^', 3, 12}}, {{1, '1', 5, 'qwerty'}})
test(t, {{':', 4, 2, 4, "uer"}}, {{1, '1', 5, 'query'}})
test(t, {{'!', 4, 'answer'}}, {{1, '1', 5, 'answer', 'query'}})
test(t, {{'#', 5, 1}}, {{1, '1', 5, 'answer'}})
test(t, {{'!', -1, 1}}, {{1, '1', 5, 'answer', 1}})
test(t, {{'!', -1, 2}}, {{1, '1', 5, 'answer', 1, 2}})
test(t, {{'!', -1, 3}}, {{1, '1', 5, 'answer', 1, 2 ,3}})
test(t, {{'#', 5, 100500}}, {{1, '1', 5, 'answer'}})
test(t, {{'=', 4, 'qwerty'}}, {{1, '1', 5, 'qwerty'}})

-- same check for negative posistion
test(t, {{'+', -2, 5}}, {{1, '1', 10, 'qwerty'}})
test(t, {{'-', -2, 3}}, {{1, '1', 7, 'qwerty'}})
test(t, {{'&', -2, 5}}, {{1, '1', 5, 'qwerty'}})
test(t, {{'|', -2, 8}}, {{1, '1', 13, 'qwerty'}})
test(t, {{'^', -2, 12}}, {{1, '1', 1, 'qwerty'}})
test(t, {{':', -1, 2, 4, "uer"}}, {{1, '1', 1, 'query'}})
test(t, {{'!', -2, 'answer'}}, {{1, '1', 1, 'answer', 'query'}})
test(t, {{'#', -1, 1}}, {{1, '1', 1, 'answer'}})
test(t, {{'=', -1, 'answer!'}}, {{1, '1', 1, 'answer!'}})

-- selective test for good multiple ops
test(t, {{'+', 3, 2}, {'!', 4, 42}}, {{1, '1', 3, 42, 'answer!'}})
test(t, {{'!', 1, 666}, {'#', 1, 1}, {'+', 3, 2}}, {{1, '1', 5, 42, 'answer!'}})
test(t, {{'!', 3, 43}, {'+', 4, 2}}, {{1, '1', 43, 7, 42, 'answer!'}})
test(t, {{'#', 3, 2}, {'=', 3, 1}, {'=', 4, '1'}}, {{1, '1', 1, '1'}})

-- all bad operations, one op, equivalent to update but error is supressed
test(t, {{'+', 4, 3}}, {{1, '1', 1, '1'}})
test(t, {{'-', 4, 3}}, {{1, '1', 1, '1'}})
test(t, {{'&', 4, 1}}, {{1, '1', 1, '1'}})
test(t, {{'|', 4, 1}}, {{1, '1', 1, '1'}})
test(t, {{'^', 4, 1}}, {{1, '1', 1, '1'}})
test(t, {{':', 3, 2, 4, "uer"}}, {{1, '1', 1, '1'}})
test(t, {{'#', 18, 1}}, {{1, '1', 1, '1'}})

-- selective test for good/bad multiple ops mix
test(t, {{'+', 3, 1}, {'+', 4, 1}}, {{1, '1', 2, '1'}})
test(t, {{'-', 4, 1}, {'-', 3, 1}}, {{1, '1', 1, '1'}})
test(t, {{'#', 18, 1}, {'|', 3, 14}}, {{1, '1', 15, '1'}})
test(t, {{'^', 42, 42}, {':', 1, 1, 1, ''}, {'^', 3, 8}}, {{1, '1', 7, '1'}})
test(t, {{'&', 3, 1}, {'&', 2, 1}, {'&', 4, 1}}, {{1, '1', 1, '1'}})

-- broken ops must raise an exception and discarded
'dump ' .. anything_to_string(box.space.s:select{})
test(t, {{'&', 'a', 3}, {'+', 3, 3}}, {{1, '1', 1, '1'}})
test(t, {{'+', 3, 3}, {'&', 3, 'a'}}, {{1, '1', 1, '1'}})
test(t, {{'+', 3}, {'&', 3, 'a'}}, {{1, '1', 1, '1'}})
test(t, {{':', 3, 3}}, {{1, '1', 1, '1'}})
test(t, {{':', 3, 3, 3}}, {{1, '1', 1, '1'}})
test(t, {{'?', 3, 3}}, {{1, '1', 1, '1'}})
'dump ' .. anything_to_string(box.space.s:select{})

-- ignoring ops for insert upsert
test({2, '2', 2, '2'}, {{}}, {{1, '1', 1, '1'}})
test({3, '3', 3, '3'}, {{'+', 3, 3}}, {{1, '1', 1, '1'}, {3, '3', 3, '3'}})

-- adding random ops
t[1] = 1
test(t, {{'+', 3, 3}, {'+', 4, 3}}, {{1, '1', 4, '1'}, {3, '3', 3, '3'}})
t[1] = 2
test(t, {{'-', 4, 1}}, {{1, '1', 4, '1'}, {2, '1', 1, 'qwerty'}, {3, '3', 3, '3'}})
t[1] = 3
test(t, {{':', 3, 3, 3, ''}, {'|', 3, 4}}, {{1, '1', 4, '1'}, {2, '1', 1, 'qwerty'}, {3, '3', 7, '3'}})

'dump ' .. anything_to_string(box.space.s:select{}) -- (1)
test_run:cmd("restart server default")
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')

test_run:cmd("setopt delimiter ';'")
function anything_to_string(tab)
    if tab == nil then
        return 'nil'
    end
    local str = '['
    local first_route = true
    local t = 0
    for k,f in pairs(tab) do
        if not first_route then str = str .. ',' end
        first_route = false
        t = t + 1
        if k ~= t then
            str = str .. k .. '='
        end
        if type(f) == 'string' then
            str = str .. "'" .. f .. "'"
        elseif type (f) == 'number' then
            str = str .. tostring(f)
        elseif type (f) == 'table' or type (f) == 'cdata' then
            str = str .. anything_to_string(f)
        else
            str = str .. '?'
        end
    end
    str = str .. ']'
    return str
end;
test_run:cmd("setopt delimiter ''");

s = box.space.s
'dump ' .. anything_to_string(box.space.s:select{})-- compare with (1) visually!

box.space.s:drop()

--
-- gh-2104: vinyl: assert in tuple_upsert_squash
--

s = box.schema.space.create('test', {engine = engine})
i = s:create_index('test')

s:replace({1, 1, 1})
box.snapshot()
s:upsert({1, 1}, {{'+', 2, 2}})
s:upsert({1, 1}, {{'+', 3, 4}})
s:select()

s:drop()

--
-- gh-2259: space:upsert() crashes in absence of indices
--
s = box.schema.space.create('test', {engine = engine})
s:upsert({1}, {})
s:drop()

--
-- gh-2461 - segfault on sparse or unordered keys.
--
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk', {parts = {1, 'unsigned', 3, 'unsigned'}})
s:upsert({100, 100, 100}, {{'+', 2, 200}})
s:upsert({200, 100, 200}, {{'+', 2, 300}})
s:upsert({300, 100, 300}, {{'+', 2, 400}})
pk:select{}
s:drop()

-- test for non-spased and non-sequential index
s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk', {parts = {2, 'unsigned', 3, 'unsigned'}})
s:upsert({100, 100, 100}, {{'+', 1, 200}})
s:upsert({200, 100, 200}, {{'+', 1, 300}})
s:upsert({300, 100, 300}, {{'+', 1, 400}})
pk:select{}
s:drop()

s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk', {parts = {3, 'unsigned', 2, 'unsigned'}})
s:upsert({100, 100, 100}, {{'+', 1, 200}})
s:upsert({200, 100, 200}, {{'+', 1, 300}})
s:upsert({300, 100, 300}, {{'+', 1, 400}})
pk:select{}
s:drop()

s = box.schema.space.create('test', {engine = engine})
pk = s:create_index('pk', {parts = {1, 'unsigned'}})
sec = s:create_index('sec', {parts = {4, 'unsigned', 2, 'unsigned', 3, 'unsigned'}})
s:replace{1, 301, 300, 300}
sec:select{}
s:upsert({1, 301, 300, 300}, {{'+', 2, 1}, {'+', 3, 1}, {'+', 4, 1}})
sec:select{}
s:upsert({1, 302, 301, 301}, {{'+', 2, 1}, {'+', 3, 1}, {'+', 4, 1}})
sec:select{}
s:upsert({2, 203, 200, 200}, {{'+', 2, 1}, {'+', 3, 1}, {'+', 4, 1}})
sec:select{}
s:replace{3, 302, 50, 100}
sec:select{}
sec:get{100, 302, 50}
sec:get{200, 203, 200}
sec:get{302, 303, 302}
s:drop()
