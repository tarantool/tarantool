
-- Test for unique indices

-- Tests for TREE index type
env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')

s0 = box.schema.space.create('my_space1', { engine = engine })
i0 = s0:create_index('my_space1_idx1', {type='TREE', parts={1, 'number'}, unique=true})

s0:insert({10})
s0:insert({11})
s0:insert({12})
s0:insert({13})
s0:select{}

s0:insert({3})
s0:insert({4})
s0:insert({5})
s0:insert({6})
s0:select{}

s0:insert({-5})
s0:insert({-6})
s0:insert({-7})
s0:insert({-8})
s0:select{}

s0:insert({-10})
s0:insert({-11})
s0:insert({-12})
s0:insert({-13})
s0:select{}

s0:insert({3.5})
s0:insert({4.5})
s0:insert({5.5})
s0:select{}

s0:insert({-3.5})
s0:insert({-4.5})
s0:insert({-5.5})
s0:select{}

s0:drop()

s1 = box.schema.space.create('my_space2', { engine = engine })
i1 = s1:create_index('my_space2_idx1', {type='TREE', parts={1, 'scalar'}, unique=true})

s1:insert({10})
s1:insert({11})
s1:insert({12})
s1:insert({13})
s1:select{}

s1:insert({3})
s1:insert({4})
s1:insert({5})
s1:insert({6})
s1:select{}

s1:insert({'ffff'})
s1:insert({'gggg'})
s1:insert({'hhhh'})
s1:select{}

s1:insert({'aaaa'})
s1:insert({'bbbb'})
s1:insert({'cccc'})
s1:select{}

s1:insert({3.5})
s1:insert({4.5})
s1:insert({5.5})
s1:select{}

s1:insert({-3.5})
s1:insert({-4.5})
s1:insert({-5.5})
s1:select{}

s1:insert({true})
s1:insert({false})
s1:insert({1})
s1:insert({0})

s1:insert({'!!!!'})
s1:insert({'????'})
s1:select{}

s1:drop()

s2 = box.schema.space.create('my_space3', { engine = engine })
i2_1 = s2:create_index('my_space3_idx1', {type='TREE', parts={1, 'scalar', 2, 'integer', 3, 'number'}, unique=true})

s2:insert({10, 1, -1, 'z', true})
s2:insert({11, 2, 2, 'g', false})
s2:insert({12, 3, -3, 'e', -100.5})
s2:insert({13, 4, 4, 'h', 200})
s2:select{}

s2:insert({3, 5, -5, 'w', 'strstr'})
s2:insert({4, 6, 6, 'q', ';;;;'})
s2:insert({5, 7, -7, 'c', '???'})
s2:insert({6, 8, 8, 'k', '!!!'})
s2:select{}

s2:insert({'ffff', 9, -9, 'm', '123'})
s2:insert({'gggg', 10, 10, 'r', '456'})
s2:insert({'hhhh', 11, -11, 'i', 55555})
s2:insert({'hhhh', 11, -10, 'i', 55556})
s2:insert({'hhhh', 11, -12, 'i', 55554})
s2:select{}

s2:insert({'aaaa', 12, 12, 'o', 333})
s2:insert({'bbbb', 13, -13, 'p', '123'})
s2:insert({'cccc', 14, 14, 'l', 123})
s2:select{}

s2:insert({3.5, 15, -15, 'n', 500})
s2:insert({4.5, 16, 16, 'b', 'ghtgtg'})
s2:insert({5.5, 17, -17, 'v', '"""""'})
s2:select{}

s2:insert({-3.5, 18, 18, 'x', '---'})
s2:insert({-4.5, 19, -19, 'a', 56.789})
s2:insert({-5.5, 20, 20, 'f', -138.4})
s2:select{}

s2:insert({true, 21, -21, 'y', 50})
s2:insert({false, 22, 22, 's', 60})

s2:insert({'!!!!', 23, -23, 'u', 0})
s2:insert({'????', 24, 24, 'j', 70})
s2:select{}

s2.index.my_space3_idx2:select{}

s2:drop()

-- Tests for NULL

mp = require('msgpack')

s4 = box.schema.space.create('my_space5', { engine = engine })
i4_1 = s4:create_index('my_space5_idx1', {type='TREE', parts={1, 'scalar', 2, 'integer', 3, 'number'}, unique=true})
s4:insert({mp.NULL, 1, 1, 1})
s4:insert({2, mp.NULL, 2, 2}) -- all nulls must fail
s4:insert({3, 3, mp.NULL, 3})
s4:insert({4, 4, 4, mp.NULL})

s4:drop()

-- Test for nonunique indices

s5 = box.schema.space.create('my_space6', { engine = engine })
i5_1 = s5:create_index('my_space6_idx1', {type='TREE', parts={1, 'unsigned'}, unique=true})
i5_2 = s5:create_index('my_space6_idx2', {type='TREE', parts={2, 'scalar'}, unique=false})

test_run:cmd("setopt delimiter ';'");
function less(a, b)
    if type(a[2]) ~= type(b[2]) then
        return type(a[2]) < type(b[2])
    end
    if type(a[2]) == 'boolean' then
        if a[2] == false and b[2] == true then
            return true
        end
    end
    if a[2] == b[2] then
        return a[1] < b[1]
    end
    return a[2] < b[2]
end;
test_run:cmd("setopt delimiter ''");
function sort(t) table.sort(t, less) return t end

s5:insert({1, "123"})
s5:insert({2, "123"})
s5:insert({3, "123"})
s5:insert({4, 123})
s5:insert({5, 123})
s5:insert({6, true})
s5:insert({7, true})
s5:insert({8, mp.NULL}) -- must fail
s5:insert({9, -40.5})
s5:insert({10, -39.5})
s5:insert({11, -38.5})
s5:insert({12, 100.5})
s5:select{}
sort(i5_2:select({123}))
sort(i5_2:select({"123"}))
sort(i5_2:select({true}))
sort(i5_2:select({false}))
sort(i5_2:select({true}))
sort(i5_2:select({-38.5}))

sort(i5_2:select({-40}, {iterator = 'GE'}))

s5:drop()

-- gh-1897 Crash on index field type 'any'

space = box.schema.space.create('test', {engine = engine})
pk = space:create_index('primary', { parts = {1, 'any'} }) --
space:insert({1})                                          -- must fail
space:insert({2})                                          --
space:drop()

-- gh-1701 allow NaN

rnd = math.random(2147483648)
ffi = require('ffi')
ffi.cdef(string.format("union nan_%s { double d; uint64_t i; }", rnd))
nan_ffi = ffi.new(string.format('union nan_%s', rnd))
nan_ffi.i = 0x7ff4000000000000
sNaN = nan_ffi.d
nan_ffi.i = 0x7ff8000000000000
qNaN = nan_ffi.d

-- basic test
space = box.schema.space.create('test', { engine = engine })
pk = space:create_index('primary', {parts = {1, 'number'}})
space:replace({sNaN, 'signaling NaN'})
space:replace({qNaN, 'quiet NaN'})
space:get{sNaN}
space:get{qNaN}
space:get{1/0}
space:get{1/0 - 1/0}
space:get{0/0}
space:select{}
space:truncate()

-- test ordering of special values
space:replace({1/0, '+inf'})
space:replace({sNaN, 'snan'})
space:replace({100})
space:replace({-1/0, '-inf'})
space:replace({50})
space:replace({qNaN, 'qnan'})

pk:get{100/0}
pk:get{sNaN}
pk:get{100}
pk:get{-100/0}
pk:get{50}
pk:get{qNaN}
pk:select({sNaN}, {iterator = 'GE'})
pk:select({1/0}, {iterator = 'LT'})

space:drop()
