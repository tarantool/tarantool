test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- These tests check that SQL savepoints properly work outside
-- transactions as well as inside transactions started in Lua.
-- gh-3313
--

box.execute('SAVEPOINT t1;');
box.execute('RELEASE SAVEPOINT t1;');
box.execute('ROLLBACK TO SAVEPOINT t1;');

box.begin() box.execute('SAVEPOINT t1;') box.execute('RELEASE SAVEPOINT t1;') box.commit();

box.begin() box.execute('SAVEPOINT t1;') box.execute('ROLLBACK TO t1;') box.commit();

box.begin() box.execute('SAVEPOINT t1;') box.commit();

box.commit();

-- These tests check that release of SQL savepoints works as desired.
-- gh-3379
test_run:cmd("setopt delimiter ';'")

release_sv = function()
    box.begin()
    box.execute('SAVEPOINT t1;')
    box.execute('RELEASE SAVEPOINT t1;')
end;
release_sv();
box.commit();

release_sv_2 = function()
    box.begin()
    box.execute('SAVEPOINT t1;')
    box.execute('SAVEPOINT t2;')
    box.execute('SAVEPOINT t3;')
    box.execute('RELEASE SAVEPOINT t2;')
    local _, err = box.execute('ROLLBACK TO t1;')
    assert(err == nil)
end;
release_sv_2();
box.commit();

release_sv_fail = function()
    box.begin()
    box.execute('SAVEPOINT t1;')
    box.execute('SAVEPOINT t2;')
    box.execute('RELEASE SAVEPOINT t2;')
    box.execute('RELEASE SAVEPOINT t1;')
    local _, err = box.execute('ROLLBACK TO t1;')
    if err ~= nil then
        return err
    end
end;
release_sv_fail();
box.commit();

-- Make sure that if the current transaction has a savepoint
-- with the same name, the old savepoint is deleted and
-- a new one is set. Note that no error should be raised.
--
collision_sv_2 = function()
    box.begin()
    box.execute('SAVEPOINT t1;')
    box.execute('SAVEPOINT t2;')
    local _,err = box.execute('SAVEPOINT t1;')
    assert(err == nil)
    box.execute('RELEASE SAVEPOINT t1;')
    local _,err = box.execute('RELEASE SAVEPOINT t1;')
    assert(err ~= nil)
    local _, err = box.execute('ROLLBACK TO t2;')
    assert(err == nil)
end;
collision_sv_2();
box.commit();
