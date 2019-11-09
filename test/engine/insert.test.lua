test_run = require('test_run')
inspector = test_run.new()
engine = inspector:get_cfg('engine')

-- insert (str)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:insert({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:insert({tostring(7)})
space:drop()


-- insert (num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
for key = 1, 100 do space:insert({key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key})) end
t
space:insert({7})
space:drop()


-- insert multi-part (num, num)
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned', 2, 'unsigned'} })
for key = 1, 100 do space:insert({key, key}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({key, key})) end
t
space:insert({7, 7})
space:drop()

-- insert with tuple.new
space = box.schema.space.create('test', { engine = engine })
index = space:create_index('primary', { type = 'tree', parts = {1, 'string'} })
for key = 1, 100 do space:insert({tostring(key)}) end
t = {}
for key = 1, 100 do table.insert(t, space:get({tostring(key)})) end
t
space:insert(box.tuple.new{tostring(7)})
space:drop()

-- In non-unique indexes select output order is undefined,
-- so it's better to additionally sort output to receive same order every time.
function sort_cmp(a, b) return a[1] < b[1] and true or false end
function sort(t) table.sort(t, sort_cmp) return t end

-- insert in space with multiple indices
space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'number', 2, 'scalar'}})
index2 = space:create_index('secondary', { type = 'tree', parts = {3, 'unsigned', 1, 'number'}})
index3 = space:create_index('third', { type = 'tree', parts = {2, 'scalar', 4, 'string'}, unique = false})
space:insert({50, 'fere', 3, 'rgrtht'})
space:insert({-14.645, true, 562, 'jknew'})
space:insert({533, 1293.352, 2132, 'hyorj'})
space:insert({4824, 1293.352, 684, 'hyorj'})
index1:select{}
index2:select{}
sort(index3:select{})
space:drop()

space = box.schema.space.create('test', { engine = engine })
index1 = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'} })
index2 = space:create_index('secondary', { type = 'tree', parts = {2, 'unsigned'} })
index3 = space:create_index('third', { type = 'tree', parts = {3, 'unsigned'}, unique = false })
space:insert({1, 1, 1})
space:insert({2, 2, 2})
space:insert({3, 3, 3})
space:select{}
space:insert({1, 2, 3})
index1:select{}
index2:select{}
sort(index3:select{})
space:drop()

-- gh-186 New implementation of box.replace does not check that tuple is
-- array
s = box.schema.space.create('s', { engine = engine })
index = s:create_index('pk')
s:insert(1)
s:insert(1, 2)
s:insert(1, 2, 3)
s:insert{1}
s:insert{2, 3}
-- xxx: silently truncates the tail - should warn perhaps
tmp = s:delete(1, 2, 3)
s:select{}
s:drop()

-- concurrent insert fail
fiber = require('fiber')
s = box.schema.space.create('s', { engine = engine })
index = s:create_index('pk')
n_workers = 3
n_success = 0
n_failed = 0
c = fiber.channel(n_workers)

inspector:cmd("setopt delimiter ';'")
for i=1,n_workers do
    fiber.create(function()
        if pcall(s.insert, s, {42}) then
            n_success = n_success + 1
        else
            n_failed = n_failed + 1
        end
        c:put(true)
    end)
end;
inspector:cmd("setopt delimiter ''");

-- Join background fibers.
for i=1,n_workers do c:get() end

n_success
n_failed

s:select{}
s:drop()
fiber = nil

-- gh-3812: Make sure that DOUBLE field type works properly.
ffi = require('ffi')

s = box.schema.space.create('s', {format = {{'i', 'integer'}, {'d', 'double'}}})
_ = s:create_index('ii')

--
-- If number of Lua type NUMBER is not integer, than it could be
-- inserted in DOUBLE field.
--
s:insert({1, 1.1})
s:insert({2, 2.5})
s:insert({3, -3.0009})

--
-- Integers of Lua type NUMBER and CDATA of type int64 or uint64
-- cannot be inserted into this field.
--
s:insert({4, 1})
s:insert({5, -9223372036854775800ULL})
s:insert({6, 18000000000000000000ULL})

--
-- To insert an integer, we must cast it to a CDATA of type DOUBLE
-- using ffi.cast(). Non-integers can also be inserted this way.
--
s:insert({7, ffi.cast('double', 1)})
s:insert({8, ffi.cast('double', -9223372036854775808)})
s:insert({9, ffi.cast('double', tonumber('123'))})
s:insert({10, ffi.cast('double', tonumber64('18000000000000000000'))})
s:insert({11, ffi.cast('double', 1.1)})
s:insert({12, ffi.cast('double', -3.0009)})

s:select()

-- The same rules apply to the key of this field:
dd = s:create_index('dd', {unique = false, parts = {{2, 'double'}}})
dd:select(1.1)
dd:select(1)
dd:select(ffi.cast('double', 1))

-- Make sure the comparisons work correctly.
dd:select(1.1, {iterator = 'ge'})
dd:select(1.1, {iterator = 'le'})
dd:select(ffi.cast('double', 1.1), {iterator = 'gt'})
dd:select(ffi.cast('double', 1.1), {iterator = 'lt'})
dd:select(1.1, {iterator = 'all'})
dd:select(1.1, {iterator = 'eq'})
dd:select(1.1, {iterator = 'req'})

s:delete(11)
s:delete(12)

-- Make sure that other operations are working correctly:
ddd = s:create_index('ddd', {parts = {{2, 'double'}}})

s:update(1, {{'=', 2, 2}})
s:insert({22, 22})
s:upsert({10, 100}, {{'=', 2, 2}})
s:upsert({100, 100}, {{'=', 2, 2}})

ddd:update(1, {{'=', 1, 70}})
ddd:delete(1)

s:update(2, {{'=', 2, 2.55}})
s:replace({22, 22.22})
s:upsert({100, 100.5}, {{'=', 2, 2}})
s:get(100)
s:upsert({10, 100.5}, {{'=', 2, 2.2}})
s:get(10)

ddd:update(1.1, {{'=', 3, 111}})
ddd:delete(1.1)

s:update(2, {{'=', 2, ffi.cast('double', 255)}})
s:replace({22, ffi.cast('double', 22)})
s:upsert({200, ffi.cast('double', 200)}, {{'=', 2, 222}})
s:get(200)
s:upsert({200, ffi.cast('double', 200)}, {{'=', 2, ffi.cast('double', 222)}})
s:get(200)

ddd:update(ffi.cast('double', 1), {{'=', 3, 7}})
ddd:delete(ffi.cast('double', 123))

s:select()

s:drop()
