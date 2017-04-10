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

