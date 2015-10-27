--
-- we actually need to know what xlogs the server creates,
-- so start from a clean state
--
-- 
-- Check how well we handle a failed log write
-- in panic_on_wal_error=false mode
--
env = require('test_run')
test_run = env.new()
test_run:cmd('restart server default with cleanup=1')

box.error.injection.set("ERRINJ_WAL_WRITE", true)
box.space._schema:insert{"key"}
test_run:cmd('restart server default')
box.space._schema:insert{"key"}
test_run:cmd('restart server default')
box.space._schema:get{"key"}
box.space._schema:delete{"key"}
-- list all the logs
name = string.match(arg[0], "([^,]+)%.lua")
require('fio').glob(name .. "/*.xlog")
test_run:cmd('restart server default with cleanup=1')
