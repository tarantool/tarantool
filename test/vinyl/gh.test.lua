fiber = require('fiber')
env = require('test_run')
test_run = env.new()

-- gh-283: hang after three creates and drops
s = box.schema.space.create('space0', {engine='vinyl'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'string'}})
s:insert{'a', 'b', 'c'}
s:drop()

s = box.schema.space.create('space0', {engine='vinyl'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'string'}})
s:insert{'a', 'b', 'c'}
t = s.index[0]:select({}, {iterator = box.index.ALL})
t
s:drop()

s = box.schema.space.create('space0', {engine='vinyl'})
i = s:create_index('space0', {type = 'tree', parts = {1, 'string'}})
s:insert{'a', 'b', 'c'}
t = s.index[0]:select({}, {iterator = box.index.ALL})
t
s:drop()


-- gh-280: crash if insert without index
s = box.schema.space.create('test', {engine='vinyl'})
s:insert{'a'}
s:drop()


-- gh-436: No error when creating temporary vinyl space
s = box.schema.space.create('tester',{engine='vinyl', temporary=true})


-- gh-432: ignored limit
s = box.schema.space.create('tester',{engine='vinyl'})
i = s:create_index('vinyl_index', {})
for v=1, 100 do s:insert({v}) end
t = s:select({''},{iterator='GT', limit =1})
t
t = s:select({},{iterator='GT', limit =1})
t
s:drop()

s = box.schema.space.create('tester', {engine='vinyl'})
i = s:create_index('vinyl_index', {type = 'tree', parts = {1, 'string'}})
for v=1, 100 do s:insert({tostring(v)}) end
t = s:select({''},{iterator='GT', limit =1})
t
t = s:select({},{iterator='GT', limit =1})
t
s:drop()


-- gh-681: support or produce error on space::alter
s = box.schema.space.create('M', {engine='vinyl'})
i = s:create_index('primary',{})
s:insert{5, 5}
s.index.primary:alter({parts={2,'unsigned'}})
s:drop()


-- gh-1008: assertion if insert of wrong type
s = box.schema.space.create('t', {engine='vinyl'})
i = s:create_index('primary',{parts={1, 'string'}})
box.space.t:insert{1,'A'}
s:drop()


-- gh-1009: search for empty string fails
s = box.schema.space.create('t', {engine='vinyl'})
i = s:create_index('primary',{parts={1, 'string'}})
s:insert{''}
#i:select{''}
i:get{''}
s:drop()


-- gh-1407: upsert generate garbage data
email_space_id = 'email'
email_space = box.schema.space.create(email_space_id, { engine = 'vinyl', if_not_exists = true })
i = email_space:create_index('primary', { parts = {1, 'string'} })

time = 1234
email = "test@domain.com"
email_hash_index = "asdfasdfs"
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:upsert({email, email_hash_index, time}, {{'!', -1, email_hash_index}, {'!', -1, time}})
box.space.email:select{email}
box.space.email:drop()

--gh-1540: vinyl: invalid results from LE/LT iterators
s = box.schema.space.create('test', { engine = 'vinyl' })
i = box.space.test:create_index('primary', { parts = { 1, 'unsigned', 2, 'unsigned' } })
for i =1,2 do for j=1,9 do box.space.test:replace({i, j}) end end
box.space.test:select({1, 999999}, {iterator = 'LE'})
box.space.test:drop()

s1 = box.schema.create_space('s1',{engine='vinyl'})
i1 = s1:create_index('primary',{parts={1,'unsigned',2,'unsigned'}})
s2 = box.schema.create_space('s2',{engine='memtx'})
i2 = s2:create_index('primary',{parts={1,'unsigned',2,'unsigned'}})
for i = 1,3 do for j = 1,5 do s1:insert{i, j} s2:insert{i, j} end end
itrs = {'GE', 'GT', 'LE', 'LT'}
good = true
test_run:cmd("setopt delimiter ';'")
function my_equal(a, b)
    if type(a) ~= type(b) then
        return false
    elseif type(a) ~= 'table' and not box.tuple.is(a) then
        return a == b
    end
    for k,v in pairs(a) do if not my_equal(b[k], v) then return false end end
    for k,v in pairs(b) do if not my_equal(a[k], v) then return false end end
    return true
end;
for i = 0,4 do
    for j = 0,6 do
        for k = 1,4 do
            opts = {iterator=itrs[k]}
            if not my_equal(s1:select({i, j}, opts), s2:select({i, j}, opts)) then
                good = false
            end
        end
    end
end;
test_run:cmd("setopt delimiter ''");
good
s1:drop()
s2:drop()

--
-- gh-1608: tuple disappears after invalid upsert
--
s = box.schema.create_space('test', {engine = 'vinyl'})
_ = s:create_index('test', {type = 'tree', parts = {1, 'unsigned', 2, 'string'}})
s:put({1, 'test', 3, 4})
s:select()
s:upsert({1, 'test', 'failed'}, {{'=', 3, 33}, {'=', 4, nil}})
s:select()
s:drop()

--
-- gh-1684: vinyl: infinite cycle on box.snapshot()
--

-- Create and drop several indices
space = box.schema.space.create('test', { engine = 'vinyl'  })
pk = space:create_index('primary')
index2 = space:create_index('secondary', { parts = {2, 'string'}  })
index3 = space:create_index('third', { parts = {3, 'string'}, unique = false  })
index2:drop()
index2 = space:create_index('secondary', { parts = {4, 'string'}  })
index3:drop()
index2:drop()
index2 = space:create_index('secondary', { parts = {2, 'string'}  })
index3 = space:create_index('third', { parts = {3, 'string'}, unique = false  })
index4 = space:create_index('fourth', { parts = {2, 'string', 3, 'string'}  })
space:drop()

space = box.schema.space.create('test', { engine = 'vinyl' })
pk = space:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}  })
box.snapshot()
space:drop()

--
-- gh-1658: auto_increment
--
space = box.schema.space.create('tweedledum', { engine = 'vinyl' })
_ = space:create_index('primary')
space:auto_increment{'a'}
space:auto_increment{'b'}
space:auto_increment{'c'}
space:select{}
space:truncate()
space:auto_increment{'a'}
space:auto_increment{'b'}
space:auto_increment{'c'}
space:select{}
space:delete{2}
space:auto_increment{'d'}
space:select{}
space:drop()

--
-- Truncate basic test
--
-- truncate

s = box.schema.space.create('name_of_space', {engine='vinyl'})
i = s:create_index('name_of_index', {type = 'tree', parts = {1, 'string'}})
s:insert{'a', 'b', 'c'}
s:select{'a'}
s:truncate()
s:select{}
s:insert{'b', 'c', 'd'}
s:select{}
s:truncate()
s:select{}
s:drop()

--
-- gh-1725: vinyl: merge iterator can't merge more than two runs 
--

s0 = box.schema.space.create('tweedledum', {engine = 'vinyl'})
i0 = s0:create_index('primary', { type = 'tree', parts = {1, 'unsigned'}})

-- integer keys
s0:replace{1, 'tuple'}
box.snapshot()
s0:replace{2, 'tuple 2'}
box.snapshot()

s0:insert{3, 'tuple 3'}

s0.index['primary']:get{1}
s0.index['primary']:get{2}
s0.index['primary']:get{3}

s0:drop()

--
-- gh-2081: snapshot hang
--

s = box.schema.space.create('tweedledum', {engine='vinyl'})
i = s:create_index('primary')
_ = s:insert{1}
_ = fiber.create(function() fiber.sleep(0.001) s:insert{2} end)
box.snapshot()
s:drop()

s = box.schema.space.create("test", {engine='vinyl'})
i1 = box.space.test:create_index('i1', {parts = {1, 'unsigned'}})
i2 = box.space.test:create_index('i2', {unique = false, parts = {2, 'unsigned'}})

count = 10000

test_run:cmd("setopt delimiter ';'")
box.begin()
for i = 1, count do
    s:replace({math.random(count), math.random(count)})
    if i % 100 == 0 then
        box.commit()
        box.begin()
    end
end
box.commit()
test_run:cmd("setopt delimiter ''");

s.index.i1:count() == s.index.i2:count()
s:drop()

-- https://github.com/tarantool/tarantool/issues/2588
max_tuple_size = box.cfg.vinyl_max_tuple_size
box.cfg { vinyl_max_tuple_size = 40 * 1024 * 1024 }
s = box.schema.space.create('vinyl', { engine = 'vinyl' })
i = box.space.vinyl:create_index('primary')
_ = s:replace({1, string.rep('x', 35 * 1024 * 1024)})
s:drop()
box.cfg { vinyl_max_tuple_size = max_tuple_size }

-- https://github.com/tarantool/tarantool/issues/2614

count = 10000

s = box.schema.space.create("test", {engine='vinyl'})
_ = s:create_index('pk')

cont = true
finished = 0

test_run:cmd("setopt delimiter ';'")
_ = fiber.create(function()
    while cont do
        s:select(math.random(count), {iterator = box.index.LE, limit = 10})
        fiber.sleep(0.01)
    end
    finished = finished + 1
end);

_ = fiber.create(function()
    while cont do
        pcall(box.snapshot)
        fiber.sleep(0.01)
    end
    finished = finished + 1
end);

for i = 1, count do
    s:replace{math.random(count)}
end;
test_run:cmd("setopt delimiter ''");

cont = false
test_run:wait_cond(function() return finished == 2 end)

s:drop()

--
-- gh-4294: assertion failure when deleting a no-op statement from
-- a secondary index write set.
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
s:replace{1, 1, 1}
box.begin()
s:update(1, {{'+', 3, 1}})
s:delete(1)
box.commit()
s:select()
s:drop()
