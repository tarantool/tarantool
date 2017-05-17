test_run = require('test_run').new()

-- Upon start the test server creates a space and populates it with
-- more tuples than can be stored in memory, which results in dumping
-- some of them to disk. If on restart, during recovery from WAL,
-- it replayed the dumped statements, it would exceed memory quota.
-- Check that it does not.

test_run:cmd('create server test with script = "vinyl/low_quota.lua"')
test_run:cmd('start server test')

test_run:cmd('switch test')
-- Create a vinyl space and trigger dump by exceeding memory quota (1 MB).
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {run_count_per_level = 10})
pad = string.rep('x', 1000)
for i = 1, 2000 do s:insert{i, pad} end
-- Save the total number of committed and dumped statements.
var = box.schema.space.create('var')
_ = var:create_index('pk', {parts = {1, 'string'}})
stat = box.info.vinyl()
_ = var:insert{'committed', stat.performance.write_count}
_ = var:insert{'dumped', stat.performance.dumped_statements}
test_run:cmd('switch default')

test_run:cmd('restart server test')

test_run:cmd('switch test')
stat = box.info.vinyl()
-- Check that we do not exceed quota.
stat.memory.used <= stat.memory.limit or {stat.memory.used, stat.memory.limit}
-- Check that we did not replay statements dumped before restart.
var = box.space.var
dumped_before = var:get('dumped')[2]
dumped_after = stat.performance.dumped_statements
committed_before = var:get('committed')[2]
committed_after = stat.performance.write_count
dumped_after == 0 or dumped_after
committed_before - dumped_before == committed_after or {dumped_before, dumped_after, committed_before, committed_after}
test_run:cmd('switch default')

test_run:cmd('stop server test')
test_run:cmd('cleanup server test')
