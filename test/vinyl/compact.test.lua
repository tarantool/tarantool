test_run = require('test_run').new()
fiber = require('fiber')
digest = require('digest')

space = box.schema.space.create("vinyl", { engine = 'vinyl' })
_= space:create_index('primary', { parts = { 1, 'unsigned' }, run_count_per_level = 2 })

function vyinfo() return box.space.vinyl.index.primary:stat() end

vyinfo().run_count == 0

-- create the frist run
space:insert({1})
space:replace({1, 2})
space:upsert({1},{{'+', 4, 5}}) -- bad upsert
require('log').info(string.rep(" ", 1024))
space:select{}
space:select{}
-- gh-1571: bad upsert should not log on reads
test_run:grep_log('default', 'UPSERT operation failed', 400) == nil

box.snapshot()
vyinfo().run_count == 1

-- create the second run
space:replace({2,2})
space:upsert({2},{{'+',4,5}}) -- bad upsert
box.snapshot() -- create the second run

-- wait for compaction
while vyinfo().run_count >= 2 do fiber.sleep(0.1) end
vyinfo().run_count == 1

-- gh-1571: bad upsert should log on compaction
test_run:grep_log('default', 'UPSERT operation failed') ~= nil

space:drop()

--
-- gh-3139: index.compact forces major compaction for all ranges
--
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {run_count_per_level = 100, page_size = 128, range_size = 1024})

test_run:cmd("setopt delimiter ';'")
function dump(big)
    local step = big and 1 or 5
    for i = 1, 20, step do
        s:replace{i, digest.urandom(1000)}
    end
    box.snapshot()
end;
function info()
    local info = s.index.pk:stat()
    return {range_count = info.range_count, run_count = info.run_count}
end
function compact()
    s.index.pk:compact()
    repeat
        fiber.sleep(0.001)
        local info = s.index.pk:stat()
    until info.range_count == info.run_count
end;
test_run:cmd("setopt delimiter ''");

-- The first run should be big enough to prevent major compaction
-- from kicking in on the next dump, because run_count_per_level
-- is ignored on the last level (see gh-3657).
dump(true)

dump()
dump()
info() -- 1 range, 3 runs

compact()
info() -- 1 range, 1 run

compact() -- no-op

dump()
dump()
dump()
info() -- 1 range, 4 runs

compact()
info() -- 2 ranges, 2 runs

compact() -- no-op

dump()
dump()
dump()
info() -- 2 ranges, 5 runs

compact()
info() -- 4 ranges, 4 runs

s:drop()
