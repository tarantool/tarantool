test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.execute('CREATE TABLE t (s1 INT PRIMARY KEY)')
box.execute('CREATE INDEX i ON t (s1)')
box.execute('ALTER TABLE t RENAME TO j3')

-- After gh-3613 fix, bug in cmp_def was discovered.
-- Comparison didn't take .opts.sql into account.
test_run:cmd('restart server default')

box.execute('DROP INDEX i ON j3')

-- Cleanup
box.execute('DROP TABLE j3')
