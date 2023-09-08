test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})
test_run:cmd("setopt delimiter ';'")

-- These tests are aimed at checking transitive transactions
-- between SQL and Lua.
--

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
box.space.t:select();
box.space.t:drop();
