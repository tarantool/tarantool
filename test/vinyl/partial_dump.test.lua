--
-- 1. Create a space which has more indexes that can be scheduled
--    for dump simultaneously (> vinyl_write_threads).
--
-- 2. Insert tuples and then update values of secondary keys.
--
-- 3. Inject a dump error for a random index. Try to make a snapshot.
--
-- 4. Restart and check the space.
--
test_run = require('test_run').new()

INDEX_COUNT = box.cfg.vinyl_write_threads * 3
assert(INDEX_COUNT < 100)

s = box.schema.space.create('test', {engine='vinyl'})
for i = 1, INDEX_COUNT do s:create_index('i' .. i, {parts = {i, 'unsigned'}}) end

test_run:cmd("setopt delimiter ';'")
function make_tuple(key, val)
    local tuple = {}
    tuple[1] = key
    for i = 2, INDEX_COUNT do
        tuple[i] = val * (i - 1)
    end
    return tuple
end
test_run:cmd("setopt delimiter ''");

for i = 1, 5 do s:insert(make_tuple(i, i)) end
for i = 1, 5 do s:replace(make_tuple(i, i * 100)) end

math.randomseed(os.time())
box.error.injection.set('ERRINJ_VY_INDEX_DUMP', math.random(INDEX_COUNT) - 1)
box.snapshot()
box.error.injection.set('ERRINJ_VY_INDEX_DUMP', -1)

test_run:cmd('restart server default')

INDEX_COUNT = box.cfg.vinyl_write_threads * 3
assert(INDEX_COUNT < 100)

s = box.space.test
s:select()

bad_index = -1
test_run:cmd("setopt delimiter ';'")
for i = 1, INDEX_COUNT - 1 do
    if s:count() ~= s.index[i]:count() then
        bad_index = i
    end
    for _, v in s.index[i]:pairs() do
        if v ~= s:get(v[1]) then
            bad_index = i
        end
    end
end
test_run:cmd("setopt delimiter ''");
bad_index < 0 or {bad_index, s.index[bad_index]:select()}

s:drop()
