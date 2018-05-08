env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default')
test_run:cmd("push filter 'error: Failed to allocate [0-9]+ ' to 'error: Failed to allocate <NUM> '")

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })
test_run:cmd("setopt delimiter ';'")
i = 1;
while true do
    space:insert{space:len(), string.rep('test', i)}
    i = i + 1
end;
space:len() > 5000;
i = 1;
while true do
    space:insert{space:len(), string.rep('test', i)}
    i = i + 1
end;
space:len() > 5000;
i = 1;
while true do
    space:insert{space:len(), string.rep('test', i)}
    i = i + 1
end;
test_run:cmd("setopt delimiter ''");
space:len() > 5000
space.index['primary']:get{0}
space.index['primary']:get{5}
space.index['primary']:get{9}
space.index['primary']:get{11}
space.index['primary']:get{15}
-- check that iterators work
i = 0
t = {}
test_run:cmd("setopt delimiter ';'")
for state, v in space:pairs() do
    table.insert(t, v)
    i = i + 1
    if i == 50 then
        break
    end
end;
test_run:cmd("setopt delimiter ''");
t
space:truncate()
space:insert{0, 'test'}
space.index['primary']:get{0}
collectgarbage('collect')
--
-- Check that statement-level rollback does not leak tuples
--
space:truncate()
function insert(a) space:insert(a) end
test_run:cmd("setopt delimiter ';'")
function dup_key()
    box.begin()
    space:insert{1}
    local i = 1
    while i < 2000 do
        local status, _ = pcall(insert, {1, string.rep('test', i)})
        if status then
            error('Unexpected success when inserting a duplicate')
        end
        if box.error.last().code ~= box.error.TUPLE_FOUND then
            box.error()
        end
        i = i + 1
    end
    box.commit()
    return i
end;
test_run:cmd("setopt delimiter ''");
dup_key()
space:select{}
--
-- Cleanup
--
space:drop()
t = nil

-- https://github.com/tarantool/tarantool/issues/962 index:delete() failed
test_run:cmd('restart server default')
arena_bytes = box.cfg.memtx_memory
str = string.rep('a', 15000) -- about size of index memory block

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })

collectgarbage('collect')
for i=1,10000 do space:insert{i, str} end
definitely_used = index:count() * 16 * 1024
2 * definitely_used > arena_bytes -- at least half memory used
to_del = index:count()
for i=1,to_del do space:delete{i} end
index:count()

collectgarbage('collect')
for i=1,10000 do space:insert{i, str} end
definitely_used = index:count() * 16 * 1024
2 * definitely_used > arena_bytes -- at least half memory used
space:truncate()
index:count()

space:drop()
str = nil

