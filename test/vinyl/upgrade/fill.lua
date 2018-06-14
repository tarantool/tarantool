--
-- This script generates a vinyl metadata log
-- containing all possible record types.
--
fiber = require 'fiber'

box.cfg{vinyl_memory = 1024 * 1024, vinyl_timeout = 1e-9, checkpoint_count = 1}

dump_trigger = box.schema.space.create('dump_trigger', {engine = 'vinyl'})
dump_trigger:create_index('pk', {run_count_per_level = 1})

-- Trigger dump of all indexes and wait for it to finish.
--
-- On hitting memory limit, vinyl dumps all existing spaces, so
-- to trigger system-wide memory dump, it is enough to insert a
-- huge tuple into one space.
--
function dump()
    local pad = string.rep('x', box.cfg.vinyl_memory / 2)
    dump_trigger:replace{1, pad}
    -- Must fail due to quota timeout, but still trigger dump.
    if pcall(dump_trigger.replace, dump_trigger, {1, pad}) then
        assert(false)
    end
    -- Wait for dump to complete.
    while box.stat.vinyl().quota.used > 0 do
        fiber.sleep(0.01)
    end
    -- Wait for compaction to collect garbage.
    while dump_trigger.index.pk:stat().run_count > 1 do
        fiber.sleep(0.01)
    end
end

--
-- Create a space:
--
--   VY_LOG_CREATE_INDEX
--   VY_LOG_INSERT_RANGE
--
s = box.schema.space.create('test', {engine = 'vinyl'})
s:create_index('i1', {parts = {1, 'unsigned'}, run_count_per_level = 1})
s:create_index('i2', {parts = {2, 'string'},   run_count_per_level = 2})

--
-- Trigger compaction:
--
--   VY_LOG_PREPARE_RUN
--   VY_LOG_CREATE_RUN
--   VY_LOG_DROP_RUN
--   VY_LOG_INSERT_SLICE
--   VY_LOG_DELETE_SLICE
--
s:insert{1, 'a'}
dump()
s:insert{2, 'b'}
dump()
s:insert{3, 'c'}
dump()

--
-- Make a snapshot and collect garbage:
--
--   VY_LOG_SNAPSHOT
--   VY_LOG_FORGET_RUN
--
-- Note, this purges:
--
--   VY_LOG_PREPARE_RUN
--   VY_LOG_DELETE_SLICE
--
box.snapshot()

--
-- Space drop:
--
-- VY_LOG_CREATE_INDEX
-- VY_LOG_DROP_INDEX
-- VY_LOG_PREPARE_RUN
-- VY_LOG_CREATE_RUN
-- VY_LOG_DROP_RUN
-- VY_LOG_INSERT_RANGE
-- VY_LOG_DELETE_RANGE
-- VY_LOG_INSERT_SLICE
-- VY_LOG_DELETE_SLICE
--
s = box.schema.space.create('test_drop', {engine = 'vinyl'})
s:create_index('i1', {parts = {1, 'unsigned'}})
s:create_index('i2', {parts = {2, 'string'}})
s:insert{11, 'aa'}
dump()
s:insert{22, 'bb'}
s:drop()

--
-- Space truncation.
--
-- Before 1.7.4-126-g2ba51ab2, implemented as index drop/create.
-- In newer versions, writes a special record:
--
-- VY_LOG_TRUNCATE_INDEX
--
s = box.schema.space.create('test_truncate', {engine = 'vinyl'})
s:create_index('i1', {parts = {1, 'unsigned'}})
s:create_index('i2', {parts = {2, 'string'}})
s:insert{1, 'a'}
dump()
s:insert{12, 'ab'}
s:truncate()
s:insert{123, 'abc'}

--
-- Create a space and trigger range splitting:
--
-- VY_LOG_CREATE_INDEX
-- VY_LOG_PREPARE_RUN
-- VY_LOG_CREATE_RUN
-- VY_LOG_DROP_RUN
-- VY_LOG_INSERT_RANGE with finite begin/end.
-- VY_LOG_DELETE_RANGE
-- VY_LOG_INSERT_SLICE with finite begin/end
-- VY_LOG_DELETE_SLICE
--
s = box.schema.space.create('test_split', {engine = 'vinyl'})
s:create_index('pk', {page_size = 4, range_size = 16, run_count_per_level = 1, run_size_ratio = 1000})
for i = 1, 4 do
    for k = 1, 8 do
        s:replace{k, i + k}
    end
    dump()
end
assert(s.index.pk:stat().range_count >= 2)

dump_trigger:drop()

os.exit(0)
