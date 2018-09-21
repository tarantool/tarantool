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

-- gh-881 iproto request with wal IO error
errinj = box.error.injection

test = box.schema.create_space('test')
_ = test:create_index('primary')

box.schema.user.grant('guest', 'write', 'space', 'test')

for i=1, box.cfg.rows_per_wal do test:insert{i, 'test'} end
c = require('net.box').connect(box.cfg.listen)

-- try to write xlog without permission to write to disk
errinj.set('ERRINJ_WAL_WRITE', true)
c.space.test:insert({box.cfg.rows_per_wal + 1,1,2,3})
errinj.set('ERRINJ_WAL_WRITE', false)

-- Cleanup
test:drop()
errinj = nil
