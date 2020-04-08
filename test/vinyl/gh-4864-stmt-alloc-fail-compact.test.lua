test_run = require('test_run').new()
fiber = require('fiber')
digest = require('digest')

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

function compact()
    s.index.pk:compact()
    repeat
        fiber.sleep(0.001)
        local info = s.index.pk:stat()
    until info.range_count == info.run_count
end;
test_run:cmd("setopt delimiter ''");

-- The first run should be big enough to prevent major compaction
-- on the next dump, because run_count_per_level is ignored on the
-- last level.
--
dump(true)
dump()
assert(s.index.pk:stat().range_count == 1)
assert(s.index.pk:stat().run_count == 2)

compact()
assert(s.index.pk:stat().range_count == 1)
assert(s.index.pk:stat().run_count == 1)

dump()
assert(s.index.pk:stat().range_count == 1)
assert(s.index.pk:stat().run_count == 2)

errinj = box.error.injection
errinj.set('ERRINJ_VY_STMT_ALLOC', 0)
-- Should finish successfully despite vy_stmt_alloc() failure.
-- Still split_range() fails, as a result we get one range
-- instead two.
--
compact()
assert(s.index.pk:stat().range_count == 1)
assert(s.index.pk:stat().run_count == 1)
assert(errinj.get('ERRINJ_VY_STMT_ALLOC') == -1)
errinj.set('ERRINJ_VY_STMT_ALLOC', -1)

s:drop()
