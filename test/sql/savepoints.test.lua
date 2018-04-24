test_run = require('test_run').new()

-- These tests check that SQL savepoints properly work outside
-- transactions as well as inside transactions started in Lua.
-- gh-3313
--

box.sql.execute('SAVEPOINT t1;');
box.sql.execute('RELEASE SAVEPOINT t1;');
box.sql.execute('ROLLBACK TO SAVEPOINT t1;');

box.begin() box.sql.execute('SAVEPOINT t1;') box.sql.execute('RELEASE SAVEPOINT t1;') box.commit();

box.begin() box.sql.execute('SAVEPOINT t1;') box.sql.execute('ROLLBACK TO t1;') box.commit();

box.begin() box.sql.execute('SAVEPOINT t1;') box.commit();

box.commit();
