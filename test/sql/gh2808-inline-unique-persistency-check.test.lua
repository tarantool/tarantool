-- Regression test for gh-2483
env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

-- Create a table and insert a datum
box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT, UNIQUE(b));]])
box.execute([[INSERT INTO t1 VALUES(1,2);]])

-- Sanity check
box.execute([[SELECT * FROM t1]])

test_run:cmd('restart server default');
-- This cmd should not fail
-- before this fix, unique index was notrecovered
-- correctly after restart (#2808)
box.execute([[INSERT INTO t1 VALUES(2,3);]])

-- Sanity check
box.execute([[SELECT * FROM t1]])

-- Cleanup
box.execute([[drop table t1;]])
