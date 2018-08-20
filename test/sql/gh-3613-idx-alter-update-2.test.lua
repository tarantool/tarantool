test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

box.sql.execute('CREATE TABLE t (s1 INT PRIMARY KEY)')
box.sql.execute('CREATE INDEX i ON t (s1)')
box.sql.execute('ALTER TABLE t RENAME TO j3')

-- After gh-3613 fix, bug in cmp_def was discovered.
-- Comparison didn't take .opts.sql into account.
test_run:cmd('restart server default')

box.sql.execute('DROP INDEX i ON j3')

-- Cleanup
box.sql.execute('DROP TABLE j3')
