test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})
test_run:cmd("setopt delimiter ';'")

-- These tests are aimed at checking transitive transactions
-- between SQL and Lua. In particular, make sure that deferred foreign keys
-- violations are passed correctly.
--

box.begin() box.execute('COMMIT');
box.begin() box.execute('ROLLBACK');
box.execute('START TRANSACTION;') box.commit();
box.execute('START TRANSACTION;') box.rollback();

box.execute('CREATE TABLE parent(id INT PRIMARY KEY, y INT UNIQUE);');
box.execute('CREATE TABLE child(id INT PRIMARY KEY, x INT REFERENCES parent(y) DEFERRABLE INITIALLY DEFERRED);');

fk_violation_1 = function()
    box.begin()
    box.execute('INSERT INTO child VALUES (1, 1);')
    local _, err = box.execute('COMMIT;')
    if err ~= nil then
        return err
    end
end;
fk_violation_1();
box.space.CHILD:select();

fk_violation_2 = function()
    box.execute('START TRANSACTION;')
    box.execute('INSERT INTO child VALUES (1, 1);')
    box.commit()
end;
fk_violation_2();
box.space.CHILD:select();

fk_violation_3 = function()
    box.begin()
    box.execute('INSERT INTO child VALUES (1, 1);')
    box.execute('INSERT INTO parent VALUES (1, 1);')
    box.commit()
end;
fk_violation_3();
box.space.CHILD:select();
box.space.PARENT:select();

-- Make sure that setting 'defer_foreign_keys' works.
--
box.execute('DROP TABLE child;')
box.execute('CREATE TABLE child(id INT PRIMARY KEY, x INT REFERENCES parent(y))')

fk_defer = function()
    box.begin()
    local _, err = box.execute('INSERT INTO child VALUES (1, 2);')
    if err ~= nil then
        return err
    end
    box.execute('INSERT INTO parent VALUES (2, 2);')
    box.commit()
end;
fk_defer();
box.space.CHILD:select();
box.space.PARENT:select();
box.space._session_settings:update('sql_defer_foreign_keys', {{'=', 2, true}})
box.rollback()
fk_defer();
box.space.CHILD:select();
box.space.PARENT:select();
box.space._session_settings:update('sql_defer_foreign_keys', {{'=', 2, false}})

-- Cleanup
box.execute('DROP TABLE child;');
box.execute('DROP TABLE parent;');

-- gh-4157: autoincrement within transaction started in SQL
-- leads to seagfault.
--
box.execute('CREATE TABLE t (id INT PRIMARY KEY AUTOINCREMENT);');
box.execute('START TRANSACTION')
box.execute('INSERT INTO t VALUES (null), (null);')
box.execute('INSERT INTO t VALUES (null), (null);')
box.execute('SAVEPOINT sp;')
box.execute('INSERT INTO t VALUES (null);')
box.execute('ROLLBACK TO sp;')
box.execute('INSERT INTO t VALUES (null);')
box.commit();
box.space.T:select();
box.space.T:drop();
