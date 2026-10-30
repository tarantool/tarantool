env = require('test_run')
test_run = env.new()
fiber = require('fiber')

-- Create table with enough tuples to make index build/alter in background.
s = box.schema.space.create('test', {engine = 'memtx'})
i = s:create_index('pk',  {type='tree', parts={{1, 'uint'}}})
for i = 1, 6000 do s:replace{i, i, "valid"} end

started = false

-- Array with origin tuples
origin = {[0] = s:get{0}, [1] = s:get{1}, [2] = s:get{2}}

-- Suite to test rolled back changes in build-in-background secondary index (unique and non-unique)

test_run:cmd("setopt delimiter ';'");

-- Utils to check if tuples were rolled back

function pk_contains_old_tuple(key)
    assert(s.index.pk:get{key} == origin[key])
end;

function sk_contains_old_tuple(key, is_unique)
    if (is_unique) then
        assert(s.index.sk:get{key} == origin[key])
    else
        assert(s.index.sk:select{key}[1] == origin[key])
    end
end;

function pk_and_sk_contains_old_tuple(key, is_unique)
    pk_contains_old_tuple(key)
    sk_contains_old_tuple(key, is_unique)
end;

-- Make fiber joinable.
function joinable(fib)
    fib:set_joinable(true)
    return fib
end;

-- Wait for fiber yield during building of index and then make all possible replaces:
-- 1. replace nil -> x
-- 2. replace y -> z
-- 3. replace w -> nil
-- and then roll them back.
function disturb()
    while not started do fiber.sleep(0) end
    box.begin()
    s:replace{0, 0, "invalid"}
    s:replace{1, 1, "changed D:"}
    i:delete{2}
    box.rollback()
end;

-- Build secondary index in background.
-- If the index will not be built in background, test will not be passed
-- because it will run out of time (disturber:join() will wait forever).
function create(is_unique)
    started = true
    s:create_index('sk', {unique=is_unique, type='tree', parts={{2, 'uint'}}})
    started = false
end;

test_run:cmd("setopt delimiter ''");

disturber = joinable(fiber.new(disturb))
creator = joinable(fiber.new(create, false))

disturber:join()
creator:join()

-- Check if all changes were rolled back in all indexes.
for i = 0, 2 do                            \
    pk_and_sk_contains_old_tuple(i, false) \
end

s.index.sk:drop()

disturber = joinable(fiber.new(disturb))
creator = joinable(fiber.new(create, true))

disturber:join()
creator:join()

-- Check if all changes were rolled back in all indexes.
for i = 0, 2 do                           \
    pk_and_sk_contains_old_tuple(i, true) \
end

s.index.sk:drop()

-- Suite to test rolled back changes in build-in-background secondary index (with rollback to savepoint)

test_run:cmd("setopt delimiter ';'");

-- Wait for fiber yield during building of index and then roll back to savepoint.
function disturb()
    while not started do fiber.sleep(0) end
    box.begin()
    s:replace{0, 0, "valid"}
    local svp = box.savepoint()
    s:replace{1, 1, "changed D:"}
    box.rollback_to_savepoint(svp)
    box.commit()
end;

test_run:cmd("setopt delimiter ''");

disturber = joinable(fiber.new(disturb))
creator = joinable(fiber.new(create, true))

disturber:join()
creator:join()

-- Check if replace was not rolled back.
assert(s.index.pk:get{0} ~= nil and s.index.pk:get{0} == s.index.sk:get{0})
-- Check if replace was rolled back.
pk_and_sk_contains_old_tuple(1, true)

s.index.pk:delete{0}
s.index.sk:drop()

-- Suite to test rolled back changes in alter-in-background primary index

test_run:cmd("setopt delimiter ';'");

-- Wait for fiber yield during building of index and then make all possible replaces:
-- 1. replace nil -> x
-- 2. replace y -> z
-- 3. replace w -> nil
-- 4. many replaces to catch segmentation fault if there are some unreferenced tuples
-- and then roll them back.
function disturb()
    while not started do fiber.sleep(0) end
    box.begin()
    s:replace{0, 0, "invalid"}
    s:replace{1, 1, "changed D:"}
    s:delete{2}
    for i = 3, 100 do
        s:replace{i, i, "new tuple must be referenced by primary key and unreferenced after rollback"}
    end
    box.rollback()
end;

-- Alter primary index in background.
-- If primary index will not be altered in background, test will not be passed
-- because it will run out of time (disturber:join() will wait forever).
function alter()
    started = true
    i:alter({parts={{field = 2, type = 'unsigned'}}})
    started = false
end;

test_run:cmd("setopt delimiter ''");

disturber = joinable(fiber.new(disturb))
alterer = joinable(fiber.new(alter))

disturber:join()
alterer:join()

-- Check if changes were rolled back.
for i = 0, 2 do              \
    pk_contains_old_tuple(i) \
end

-- If some tuples are not referenced, garbage collector will delete them
-- and we will catch segmentation fault on s:drop().
collectgarbage("collect")
s:drop()

origin = nil
started = nil
