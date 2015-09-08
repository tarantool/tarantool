s = box.schema.space.create('tweedledum')
index = s:create_index('pk')

-- test delete field
s:insert{1000001, 1000002, 1000003, 1000004, 1000005}
s:update({1000001}, {{'#', 1, 1}})
s:truncate()

-- test arithmetic
s:insert{1, 0}
s:update(1, {{'+', 2, 10}})
s:update(1, {{'+', 2, 15}})
s:update(1, {{'-', 2, 5}})
s:update(1, {{'-', 2, 20}})
s:update(1, {{'|', 2, 0x9}})
s:update(1, {{'|', 2, 0x6}})
s:update(1, {{'&', 2, 0xabcde}})
s:update(1, {{'&', 2, 0x2}})
s:update(1, {{'^', 2, 0xa2}})
s:update(1, {{'^', 2, 0xa2}})
s:truncate()

-- test delete multiple fields
s:insert{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15}
s:update({0}, {{'#', 42, 1}})
s:update({0}, {{'#', 4, 'abirvalg'}})
s:update({0}, {{'#', 2, 1}, {'#', 4, 2}, {'#', 6, 1}})
s:update({0}, {{'#', 4, 3}})
s:update({0}, {{'#', 5, 123456}})
s:update({0}, {{'#', 3, 4294967295}})
s:update({0}, {{'#', 2, 0}})
s:truncate()

-- test insert field
s:insert{1, 3, 6, 9}
s:update({1}, {{'!', 2, 2}})
s:update({1}, {{'!', 4, 4}, {'!', 4, 5}, {'!', 5, 7}, {'!', 5, 8}})
s:update({1}, {{'!', 10, 10}, {'!', 10, 11}, {'!', 10, 12}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'#', 2, 1}, {'!', 2, 'inserted tuple'}, {'=', 3, 'set tuple'}})
s:truncate()
s:insert{1, 'tuple'}
s:update({1}, {{'=', 2, 'set tuple'}, {'!', 2, 'inserted tuple'}, {'#', 3, 1}})
s:update({1}, {{'!', 1, 3}, {'!', 1, 2}})
s:truncate()

-- test update's assign opearations
s:replace{1, 'field string value'}
s:update({1}, {{'=', 2, 'new field string value'}, {'=', 3, 42}, {'=', 4, 0xdeadbeef}})

-- test multiple update opearations on the same field
s:update({1}, {{'+', 3, 16}, {'&', 4, 0xffff0000}, {'|', 4, 0x0000a0a0}, {'^', 4, 0xffff00aa}})

-- test update splice operation
s:replace{1953719668, 'something to splice'}
s:update(1953719668, {{':', 2, 1, 4, 'no'}})
s:update(1953719668, {{':', 2, 1, 2, 'every'}})
-- check an incorrect offset
s:update(1953719668, {{':', 2, 100, 2, 'every'}})
s:update(1953719668, {{':', 2, -100, 2, 'every'}})
s:truncate()
s:insert{1953719668, 'hello', 'october', '20th'}:unpack()
s:truncate()
s:insert{1953719668, 'hello world'}
s:update(1953719668, {{'=', 2, 'bye, world'}})
s:delete{1953719668}

s:replace({10, 'abcde'})
s:update(10,  {{':', 2, 0, 0, '!'}})
s:update(10,  {{':', 2, 1, 0, '('}})
s:update(10,  {{':', 2, 2, 0, '({'}})
s:update(10,  {{':', 2, -1, 0, ')'}})
s:update(10,  {{':', 2, -2, 0, '})'}})

-- test update delete operations
s:update({1}, {{'#', 4, 1}, {'#', 3, 1}})

-- test update insert operations
s:update({1}, {{'!', 2, 1}, {'!', 2, 2}, {'!', 2, 3}, {'!', 2, 4}})

-- s:update: zero field
s:insert{48}
s:update(48, {{'=', 0, 'hello'}})

-- s:update: push/pop fields
s:insert{1684234849}
s:update({1684234849}, {{'#', 2, 1}})
s:update({1684234849}, {{'!', -1, 'push1'}})
s:update({1684234849}, {{'!', -1, 'push2'}})
s:update({1684234849}, {{'!', -1, 'push3'}})
s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap1'}})
s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap2'}})
s:update({1684234849}, {{'#', 2, 1}, {'!', -1, 'swap3'}})
s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop1'}})
s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop2'}})
s:update({1684234849}, {{'#', -1, 1}, {'!', -1, 'noop3'}})

--
-- negative indexes
--

box.tuple.new({1, 2, 3, 4, 5}):update({{'!', 0, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -1, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -3, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -5, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -6, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -7, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'!', -100500, 'Test'}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'=', 0, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -1, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -3, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -5, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -6, 'Test'}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'=', -100500, 'Test'}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'+', 0, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -1, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -3, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -5, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -6, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'+', -100500, 100}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'|', 0, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -1, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -3, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -5, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -6, 100}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'|', -100500, 100}})

box.tuple.new({1, 2, 3, 4, 5}):update({{'#', 0, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -1, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -3, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -5, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -6, 1}})
box.tuple.new({1, 2, 3, 4, 5}):update({{'#', -100500, 1}})

--
-- #416: UPDATEs from Lua can't be properly restored due to one based indexing
--
--# stop server default
--# start server default

s = box.space.tweedledum
s:select{}
s:truncate()
s:drop()

-- #521: Cryptic error message in update operation
s = box.schema.space.create('tweedledum')
index = s:create_index('pk')
s:insert{1, 2, 3}
s:update({1})
s:update({1}, {'=', 1, 1})
s:drop()

-- #528: Different types in arithmetical update, overflow check
ffi = require('ffi')
s = box.schema.create_space('tweedledum')
index = s:create_index('pk')
s:insert{0, -1}
-- + --
s:update({0}, {{'+', 2, "a"}}) -- err
s:update({0}, {{'+', 2, 10}}) -- neg(ative) + pos(itive) = pos(itive) 9
s:update({0}, {{'+', 2, 5}}) -- pos + pos = pos 14
s:update({0}, {{'+', 2, -4}}) -- pos + neg = pos 10
s:update({0}, {{'+', 2, -22}}) -- pos + neg = neg -12
s:update({0}, {{'+', 2, -3}}) -- neg + neg = neg -15
s:update({0}, {{'+', 2, 7}}) -- neg + pos = neg -8
-- - --
s:update({0}, {{'-', 2, "a"}}) -- err
s:update({0}, {{'-', 2, 16}}) -- neg(ative) - pos(itive) = neg(ative) -24
s:update({0}, {{'-', 2, -4}}) -- neg - neg = neg 20
s:update({0}, {{'-', 2, -32}}) -- neg - neg = pos 12
s:update({0}, {{'-', 2, 3}}) -- pos - pos = pos 9
s:update({0}, {{'-', 2, -5}}) -- pos - neg = pos 14
s:update({0}, {{'-', 2, 17}}) -- pos - pos = neg -3
-- bit --
s:replace{0, 0} -- 0
s:update({0}, {{'|', 2, 24}}) -- 24
s:update({0}, {{'|', 2, 2}}) -- 26
s:update({0}, {{'&', 2, 50}}) -- 18
s:update({0}, {{'^', 2, 6}}) -- 20
s:update({0}, {{'|', 2, -1}}) -- err
s:update({0}, {{'&', 2, -1}}) -- err
s:update({0}, {{'^', 2, -1}}) -- err
s:replace{0, -1} -- -1
s:update({0}, {{'|', 2, 2}}) -- err
s:update({0}, {{'&', 2, 40}}) -- err
s:update({0}, {{'^', 2, 6}}) -- err
s:replace{0, 1.5} -- 1.5
s:update({0}, {{'|', 2, 2}}) -- err
s:update({0}, {{'&', 2, 40}}) -- err
s:update({0}, {{'^', 2, 6}}) -- err
-- double
s:replace{0, 5} -- 5
s:update({0}, {{'+', 2, 1.5}}) -- int + double = double 6.5
s:update({0}, {{'|', 2, 2}}) -- err (double!)
s:update({0}, {{'-', 2, 0.5}}) -- double - double = double 6
s:update({0}, {{'+', 2, 1.5}}) -- double + double = double 7.5
-- float
s:replace{0, ffi.new("float", 1.5)} -- 1.5
s:update({0}, {{'+', 2, 2}}) -- float + int = float 3.5
s:update({0}, {{'+', 2, ffi.new("float", 3.5)}}) -- float + int = float 7
s:update({0}, {{'|', 2, 2}}) -- err (float!)
s:update({0}, {{'-', 2, ffi.new("float", 1.5)}}) -- float - float = float 5.5
s:update({0}, {{'+', 2, ffi.new("float", 3.5)}}) -- float + float = float 9
s:update({0}, {{'-', 2, ffi.new("float", 9)}}) -- float + float = float 0
s:update({0}, {{'+', 2, ffi.new("float", 1.2)}}) -- float + float = float 1.2
-- overflow --
s:replace{0, 0xfffffffffffffffeull}
s:update({0}, {{'+', 2, 1}}) -- ok
s:update({0}, {{'+', 2, 1}}) -- overflow
s:update({0}, {{'+', 2, 100500}}) -- overflow
s:replace{0, 1}
s:update({0}, {{'+', 2, 0xffffffffffffffffull}})  -- overflow
s:replace{0, -1}
s:update({0}, {{'+', 2, 0xffffffffffffffffull}})  -- ok
s:replace{0, 0}
s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})  -- ok
s:replace{0, -1}
s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})  -- ok
s:replace{0, -2}
s:update({0}, {{'-', 2, 0x7fffffffffffffffull}})  -- overflow
s:replace{0, 1}
s:update({0}, {{'-', 2, 0xffffffffffffffffull}})  -- overflow
s:replace{0, 0xffffffffffffffefull}
s:update({0}, {{'-', 2, -16}})  -- ok
s:update({0}, {{'-', 2, -16}})  -- overflow
s:replace{0, -0x4000000000000000ll}
s:update({0}, {{'+', 2, -0x4000000000000000ll}})  -- ok
s:replace{0, -0x4000000000000000ll}
s:update({0}, {{'+', 2, -0x4000000000000001ll}})  -- overflow

--UPSERT https://github.com/tarantool/tarantool/issues/905
s:delete{0}
s:upsert({0}, {{'+', 2, 2}}) -- wrong usage
s:select{0}
s:upsert({0}, {{'+', 2, 2}}, {0, 0})
s:select{0}
s:delete{0}
s:upsert({0}, {{'+', 2, 2}}, {0, 0, 0})
s:select{0}
s:delete{0}
s:upsert({0}, {{'+', 2, 2}}, {0})
s:select{0}
s:replace{0, 1, 2, 4}
s:upsert({0}, {{'+', 2, 2}}, {0, 0, "you will not see it"})
s:select{0}
s:replace{0, -0x4000000000000000ll}
s:upsert({0}, {{'+', 2, -0x4000000000000001ll}}, {0})  -- overflow
s:select{0}
s:replace{0, "thing"}
s:upsert({0}, {{'+', 2, 2}}, {0, "nothing"})
s:select{0}
s:delete{0}
s:upsert({0}, {{'+', 2, 2}}, {0, "thing"})
s:select{0}
s:replace{0, 1, 2}
s:upsert({0}, {{'!', 42, 42}}, {0})
s:select{0}
s:upsert({0}, {{'#', 42, 42}}, {0})
s:select{0}
s:upsert({0}, {{'=', 42, 42}}, {0})
s:select{0}
s:replace{0, 1.5}
s:upsert({0}, {{'|', 1, 255}}, {0})
s:select{0}
s:replace{0, 1.5}
s:replace{0, 'something to splice'}
s:upsert({0}, {{':', 2, 1, 4, 'no'}}, {0})
s:select{0}
s:upsert({0}, {{':', 2, 1, 2, 'every'}}, {0})
s:select{0}
s:upsert({0}, {{':', 2, -100, 2, 'every'}}, {0})
s:select{0}

s:drop()


--UPSERT https://github.com/tarantool/tarantool/issues/966
--# setopt delimiter ';'
function thing_to_string(tab)
    if tab == nil then
        return 'nil'
    end
    local str = '['
    local bazaranet = true
    local t = 0
    for k,f in pairs(tab) do
        if not bazaranet then str = str .. ',' end
        bazaranet = false
        t = t + 1
        if k ~= t then
            str = str .. k .. '='
        end
        if type(f) == 'string' then
            str = str .. "'" .. f .. "'"
        elseif type (f) == 'number' then
            str = str .. tostring(f)
        elseif type (f) == 'table' or type (f) == 'cdata' then
            str = str .. thing_to_string(f)
        else
            str = str .. '?'
        end
    end
    str = str .. ']'
    return str
end;

function thing_equal(tab1, tab2)
    local type1 = type(tab1) == 'cdata' and 'table' or type(tab1)
    local type2 = type(tab2) == 'cdata' and 'table' or type(tab2)
    if type1 ~= type2 then
        return false
    end
    if type1 ~= 'table' then
        return tab1 == tab2
    end
    for k,v in pairs(tab1) do
        if not thing_equal(v, tab2[k]) then
            return false
        end
    end
    for k,v in pairs(tab2) do
        if not thing_equal(v, tab1[k]) then
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

function test(key, ops, deft, expect)
    box.space.s:upsert(key, ops, deft)
    if (thing_equal(box.space.s:select{}, expect)) then
        return 'upsert('.. thing_to_string(key) .. ', ' ..
                thing_to_string(ops) .. ', ' .. thing_to_string(deft) ..
                ') OK ' .. thing_to_string(box.space.s:select{})
    end
    return 'upsert('.. thing_to_string(key) .. ', ' ..
            thing_to_string(ops) .. ', ' .. thing_to_string(deft) ..
            ') FAILED, got ' .. thing_to_string(box.space.s:select{}) ..
            ' expected ' .. thing_to_string(expect)
end;

function test2(key, ops, deft, expect)
    box.space.s.index.i2:upsert(key, ops, deft)
    if (thing_equal(box.space.s:select{}, expect)) then
        return 'upsert('.. thing_to_string(key) .. ', ' ..
                thing_to_string(ops) .. ', ' .. thing_to_string(deft) ..
                ') OK ' .. thing_to_string(box.space.s:select{})
    end
    return 'upsert('.. thing_to_string(key) .. ', ' ..
            thing_to_string(ops) .. ', ' .. thing_to_string(deft) ..
            ') FAILED, got ' .. thing_to_string(box.space.s:select{}) ..
            ' expected ' .. thing_to_string(expect)
end;
--# setopt delimiter ''

engine = 'memtx'
s = box.schema.space.create('s', {engine = engine})
index1 = s:create_index('i1')
if engine == 'memtx' then index2 = s:create_index('i2', {parts = {2, 'str'}}) end

t = {1, '1', 1, 'qwerty'}
s:insert(t)

-- all good operations, one op, equivalent to update
test({1}, {{'+', 3, 5}}, t, {{1, '1', 6, 'qwerty'}})
test({1}, {{'-', 3, 3}}, t, {{1, '1', 3, 'qwerty'}})
test({1}, {{'&', 3, 5}}, t, {{1, '1', 1, 'qwerty'}})
test({1}, {{'|', 3, 8}}, t, {{1, '1', 9, 'qwerty'}})
test({1}, {{'^', 3, 12}}, t, {{1, '1', 5, 'qwerty'}})
test({1}, {{':', 4, 2, 4, "uer"}}, t, {{1, '1', 5, 'query'}})
test({1}, {{'!', 4, 'answer'}}, t, {{1, '1', 5, 'answer', 'query'}})
test({1}, {{'#', 5, 1}}, t, {{1, '1', 5, 'answer'}})
test({1}, {{'!', -1, 1}}, t, {{1, '1', 5, 'answer', 1}})
test({1}, {{'!', -1, 2}}, t, {{1, '1', 5, 'answer', 1, 2}})
test({1}, {{'!', -1, 3}}, t, {{1, '1', 5, 'answer', 1, 2 ,3}})
test({1}, {{'#', 5, 100500}}, t, {{1, '1', 5, 'answer'}})
test({1}, {{'=', 4, 'qwerty'}}, t, {{1, '1', 5, 'qwerty'}})

-- same check for negative posistion
test({1}, {{'+', -2, 5}}, t, {{1, '1', 10, 'qwerty'}})
test({1}, {{'-', -2, 3}}, t, {{1, '1', 7, 'qwerty'}})
test({1}, {{'&', -2, 5}}, t, {{1, '1', 5, 'qwerty'}})
test({1}, {{'|', -2, 8}}, t, {{1, '1', 13, 'qwerty'}})
test({1}, {{'^', -2, 12}}, t, {{1, '1', 1, 'qwerty'}})
test({1}, {{':', -1, 2, 4, "uer"}}, t, {{1, '1', 1, 'query'}})
test({1}, {{'!', -2, 'answer'}}, t, {{1, '1', 1, 'answer', 'query'}})
test({1}, {{'#', -1, 1}}, t, {{1, '1', 1, 'answer'}})
test({1}, {{'=', -1, 'answer!'}}, t, {{1, '1', 1, 'answer!'}})

-- selective test for good multiple ops
test({1}, {{'+', 3, 2}, {'!', 4, 42}}, t, {{1, '1', 3, 42, 'answer!'}})
test({1}, {{'!', 1, 666}, {'#', 1, 1}, {'+', 3, 2}}, t, {{1, '1', 5, 42, 'answer!'}})
test({1}, {{'!', 3, 43}, {'+', 4, 2}}, t, {{1, '1', 43, 7, 42, 'answer!'}})
test({1}, {{'#', 3, 2}, {'=', 3, 1}, {'=', 4, '1'}}, t, {{1, '1', 1, '1'}})

-- all bad operations, one op, equivalent to update but error is supressed
test({1}, {{'+', 4, 3}}, t, {{1, '1', 1, '1'}})
test({1}, {{'-', 4, 3}}, t, {{1, '1', 1, '1'}})
test({1}, {{'&', 4, 1}}, t, {{1, '1', 1, '1'}})
test({1}, {{'|', 4, 1}}, t, {{1, '1', 1, '1'}})
test({1}, {{'^', 4, 1}}, t, {{1, '1', 1, '1'}})
test({1}, {{':', 3, 2, 4, "uer"}}, t, {{1, '1', 1, '1'}})
test({1}, {{'!', 18, 'answer'}}, t, {{1, '1', 1, '1'}})
test({1}, {{'#', 18, 1}}, t, {{1, '1', 1, '1'}})
test({1}, {{'=', 18, 'qwerty'}}, t, {{1, '1', 1, '1'}})

-- selective test for good/bad multiple ops mix
test({1}, {{'+', 3, 1}, {'+', 4, 1}}, t, {{1, '1', 2, '1'}})
test({1}, {{'-', 4, 1}, {'-', 3, 1}}, t, {{1, '1', 1, '1'}})
test({1}, {{'#', 18, 1}, {'|', 3, 14}, {'!', 18, '!'}}, t, {{1, '1', 15, '1'}})
test({1}, {{'^', 42, 42}, {':', 1, 1, 1, ''}, {'^', 3, 8}}, t, {{1, '1', 7, '1'}})
test({1}, {{'&', 3, 1}, {'&', 2, 1}, {'&', 4, 1}}, t, {{1, '1', 1, '1'}})

-- broken ops must raise an exception and discarded
'dump ' .. thing_to_string(box.space.s:select{})
test({1}, {{'&', 'a', 3}, {'+', 3, 3}}, t, {{1, '1', 1, '1'}})
test({1}, {{'+', 3, 3}, {'&', 3, 'a'}}, t, {{1, '1', 1, '1'}})
test({1}, {{'+', 3}, {'&', 3, 'a'}}, t, {{1, '1', 1, '1'}})
test({1}, {{':', 3, 3}}, t, {{1, '1', 1, '1'}})
test({1}, {{':', 3, 3, 3}}, t, {{1, '1', 1, '1'}})
test({1}, {{'?', 3, 3}}, t, {{1, '1', 1, '1'}})
'dump ' .. thing_to_string(box.space.s:select{})

-- update by secondary index (memtx only)
engine ~= 'memtx' and 'skipped' or test2({'1'}, {{'+', 3, 3}}, t, {{1, '1', 4, '1'}})
engine ~= 'memtx' and 'skipped' or test2({'1'}, {{'+', 1, 3}}, t, {{1, '1', 4, '1'}})
engine ~= 'memtx' and 'skipped' or test2({'1'}, {{'+', 3, 6}, {':', 4, 2, 0, '!'}}, t, {{1, '1', 10, '1!'}})
engine ~= 'memtx' and 'skipped' or test2({'1'}, {{'+', 3, 1}, {'+', 4, 1}}, t, {{1, '1', 11, '1!'}})
engine ~= 'memtx' and 'skipped' or test2({'1'}, {{'+', 4, 1}, {'+', 3, 1}}, t, {{1, '1', 12, '1!'}})
engine ~= 'memtx' and 'skipped' or test2({'1'}, {{'-', -2, 11}, {'=', 4, '1'}}, t, {{1, '1', 1, '1'}})

-- ignoring ops for insert upsert
test({2}, {{}}, {2, '2', 2, '2'}, {{1, '1', 1, '1'}, {2, '2', 2, '2'}})
-- ignoring incorrespondance to key
test({222}, {{}}, {3, '3', 3, '3'}, {{1, '1', 1, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})
-- but cannot ignore key conflict
test({222}, {{}}, {2, '2', 2, '2'}, {{1, '1', 1, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})
test({222}, {{}}, {'2', '2', 2, '2'}, {{1, '1', 1, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})
-- for secondary index too
engine ~= 'memtx' and 'skipped' or test({222}, {{}}, {18, '2', 2, '2'}, {{1, '1', 1, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})
engine ~= 'memtx' and 'skipped' or test({222}, {{}}, {18, 2, 2, '2'}, {{1, '1', 1, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})

-- adding random ops
test({1}, {{'+', 3, 3}, {'+', 4, 3}}, t, {{1, '1', 4, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})
test({2}, {{'-', 4, 1}}, t, {{1, '1', 4, '1'}, {2, '2', 2, '2'}, {3, '3', 3, '3'}})
test({3}, {{':', 3, 3, 3, ''}, {'|', 3, 4}}, t, {{1, '1', 4, '1'}, {2, '2', 2, '2'}, {3, '3', 7, '3'}})

'dump ' .. thing_to_string(box.space.s:select{}) -- (1)
--# stop server default
--# start server default

--# setopt delimiter ';'
function thing_to_string(tab)
    if tab == nil then
        return 'nil'
    end
    local str = '['
    local bazaranet = true
    local t = 0
    for k,f in pairs(tab) do
        if not bazaranet then str = str .. ',' end
        bazaranet = false
        t = t + 1
        if k ~= t then
            str = str .. k .. '='
        end
        if type(f) == 'string' then
            str = str .. "'" .. f .. "'"
        elseif type (f) == 'number' then
            str = str .. tostring(f)
        elseif type (f) == 'table' or type (f) == 'cdata' then
            str = str .. thing_to_string(f)
        else
            str = str .. '?'
        end
    end
    str = str .. ']'
    return str
end;
--# setopt delimiter ''

s = box.space.s
'dump ' .. thing_to_string(box.space.s:select{})-- compare with (1) visually!

box.space.s:drop()
