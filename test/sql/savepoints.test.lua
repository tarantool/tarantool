test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

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

-- These tests check that release of SQL savepoints works as desired.
-- gh-3379
test_run:cmd("setopt delimiter ';'")

release_sv = function()
    box.begin()
    box.sql.execute('SAVEPOINT t1;')
    box.sql.execute('RELEASE SAVEPOINT t1;')
end;
release_sv();
box.commit();

release_sv_fail = function()
    box.begin()
    box.sql.execute('SAVEPOINT t1;')
    box.sql.execute('SAVEPOINT t2;')
    box.sql.execute('RELEASE SAVEPOINT t2;')
    box.sql.execute('RELEASE SAVEPOINT t1;')
    box.sql.execute('ROLLBACK TO t1;')
end;
release_sv_fail();
box.commit();
