test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

work_dir = 'sql/upgrade/1.10/'
test_run:cmd('create server upgrade with script="sql/upgrade/upgrade.lua", workdir="' .. work_dir .. '"')
test_run:cmd('start server upgrade')

test_run:switch('upgrade')

-- test system tables
box.space._space.index['name']:get('_trigger')
box.space._space.index['name']:get('_sql_stat1')
box.space._space.index['name']:get('_sql_stat4')

box.space._index:get({box.space._space.index['name']:get('_trigger').id, 0})
box.space._index:get({box.space._space.index['name']:get('_sql_stat1').id, 0})
box.space._index:get({box.space._space.index['name']:get('_sql_stat4').id, 0})

box.space._schema:format()

-- test data migration
box.space._space.index['name']:get('T1')
box.space._index:get({box.space._space.index['name']:get('T1').id, 0})

-- test system tables functionality
box.sql.execute("CREATE TABLE t(x INTEGER PRIMARY KEY);")
box.sql.execute("CREATE TABLE t_out(x INTEGER PRIMARY KEY);")
box.sql.execute("CREATE TRIGGER t1t AFTER INSERT ON t BEGIN INSERT INTO t_out VALUES(1); END;")
box.sql.execute("CREATE TRIGGER t2t AFTER INSERT ON t BEGIN INSERT INTO t_out VALUES(2); END;")
box.space._space.index['name']:get('T')
box.space._space.index['name']:get('T_OUT')
t1t = box.space._trigger:get('T1T')
t2t = box.space._trigger:get('T2T')
t1t.name
t1t.opts
t2t.name
t2t.opts
assert(t1t.space_id == t2t.space_id)
assert(t1t.space_id == box.space.T.id)

box.sql.execute("INSERT INTO T VALUES(1);")
box.space.T:select()
box.space.T_OUT:select()
box.sql.execute("SELECT * FROM T")
box.sql.execute("SELECT * FROM T")


box.sql.execute("DROP TABLE T;")
box.sql.execute("DROP TABLE T_OUT;")


test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('cleanup server upgrade')
