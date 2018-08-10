test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

box.sql.execute('CREATE TABLE t (s1 INT PRIMARY KEY)')
box.sql.execute('CREATE INDEX i ON t (s1)')
box.sql.execute('ALTER TABLE t RENAME TO j3')

-- Due to gh-3613, next stmt caused segfault
box.sql.execute('DROP INDEX i ON j3')

-- Make sure that no artifacts remain after restart.
box.snapshot()
test_run:cmd('restart server default')
box.sql.execute('DROP INDEX i ON j3')

box.sql.execute('CREATE INDEX i ON j3 (s1)')

-- Check that _index was altered properly
box.snapshot()
test_run:cmd('restart server default')

box.sql.execute('DROP INDEX i ON j3')

-- Cleanup
box.sql.execute('DROP TABLE j3')
