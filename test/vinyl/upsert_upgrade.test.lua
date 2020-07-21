test_run = require('test_run').new()

-- Upsert's internal format have changed: now update operations are stored
-- with additional map package. Let's test backward compatibility.
-- Snapshot (i.e. run files) contain following statements:

-- s = box.schema.create_space('test', {engine = 'vinyl'})
-- pk = s:create_index('pk')
-- s:insert({1, 2})
-- box.snapshot()
-- s:upsert({1, 0}, {{'+', 2, 1}})
-- s:upsert({1, 0}, {{'-', 2, 2}})
-- s:upsert({2, 0}, {{'+', 2, 1}})
-- s:upsert({2, 0}, {{'-', 2, 2}})
-- s:upsert({1, 0}, {{'=', 2, 2}})
-- s:upsert({1, 0}, {{'-', 2, 2}})
-- box.snapshot()
--
-- Make sure that upserts will be parsed and squashed correctly.
--

dst_dir = 'vinyl/upgrade/upsert/'

test_run:cmd('create server upgrade with script="vinyl/upgrade.lua", workdir="' .. dst_dir .. '"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

box.space.test:select()

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('cleanup server upgrade')
