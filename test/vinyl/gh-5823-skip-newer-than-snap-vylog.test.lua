test_run = require('test_run').new()

-- Test is about following scenario:
-- 1. There's both memtx and vinyl data;
-- 2. User starts checkpoint process;
-- 3. In the most unsuitable moment instance crashes;
-- 4. Recovering in the casual mode successes.
--
test_run:cmd("create server test with script='vinyl/gh-5823-crash_snapshot.lua'")
test_run:cmd("start server test with args='2' with crash_expected=True")
test_run:cmd("start server test with args='1'")
test_run:cmd("switch test")
box.space.test_v:select({5})
test_run:cmd("switch default")
test_run:cmd("stop server test")
test_run:cmd("cleanup server test")
test_run:cmd("delete server test")
