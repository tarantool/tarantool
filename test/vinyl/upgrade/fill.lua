--
-- This script generates a vinyl metadata log
-- containing all possible record types.
--
fiber = require 'fiber'

box.cfg{vinyl_memory = 1024 * 1024, vinyl_timeout = 0.1}

dump_trigger = box.schema.space.create('dump_trigger', {engine = 'vinyl'})
dump_trigger:create_index('pk')

-- Trigger dump of all indexes and wait for it to finish.
--
-- We trigger dump by attempting to insert a huge (> memory limit)
-- tuple into a vinyl memory space. Before failing on timeout this
-- makes the scheduler force dump.
function dump()
    pcall(dump_trigger.insert, dump_trigger,
          {1, string.rep('x', 1024 * 1024)})
    while box.info.vinyl().memory.used > 0 do
        fiber.sleep(0.1)
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
-- Make a snapshot:
--
--   VY_LOG_SNAPSHOT
--
-- Note, this purges:
--
--   VY_LOG_PREPARE_RUN
--   VY_LOG_DELETE_SLICE
--
box.snapshot()

--
-- Collect garbage:
--
-- VY_LOG_FORGET_RUN
--
box.internal.gc.run(box.info.signature)

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
-- Currently, implemented as index drop/create.
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
assert(s.index.pk:info().range_count >= 2)

dump_trigger:drop()

os.exit(0)
