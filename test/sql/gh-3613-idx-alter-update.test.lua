test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.execute('CREATE TABLE t (s1 INT PRIMARY KEY)')
box.execute('CREATE INDEX i ON t (s1)')
box.execute('ALTER TABLE t RENAME TO j3')

-- Due to gh-3613, next stmt caused segfault
box.execute('DROP INDEX i ON j3')

-- Make sure that no artifacts remain after restart.
box.snapshot()
test_run:cmd('restart server default')
box.execute('DROP INDEX i ON j3')

box.execute('CREATE INDEX i ON j3 (s1)')

-- Check that _index was altered properly
box.snapshot()
test_run:cmd('restart server default')

box.execute('DROP INDEX i ON j3')

-- Cleanup
box.execute('DROP TABLE j3')
