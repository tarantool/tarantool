test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

work_dir = 'sql/upgrade/1.10/'
test_run:cmd('create server upgrade with script="sql/upgrade/upgrade.lua", workdir="' .. work_dir .. '"')
test_run:cmd('start server upgrade')

test_run:switch('upgrade')

-- test system tables
box.space._space.index['name']:get('_trigger')

box.space._index:get({box.space._space.index['name']:get('_trigger').id, 0})

box.space._schema:format()

-- test data migration
box.space._space.index['name']:get('T1')
box.space._index:get({box.space._space.index['name']:get('T1').id, 0})

-- test system tables functionality
box.execute("CREATE TABLE t(x INTEGER PRIMARY KEY);")
box.execute("CREATE TABLE t_out(x INTEGER PRIMARY KEY);")
box.execute("CREATE TRIGGER t1t AFTER INSERT ON t FOR EACH ROW BEGIN INSERT INTO t_out VALUES(1); END;")
box.execute("CREATE TRIGGER t2t AFTER INSERT ON t FOR EACH ROW BEGIN INSERT INTO t_out VALUES(2); END;")
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

box.execute("INSERT INTO T VALUES(1);")
box.space.T:select()
box.space.T_OUT:select()
box.execute("SELECT * FROM T")
box.execute("SELECT * FROM T")


box.execute("DROP TABLE T;")
box.execute("DROP TABLE T_OUT;")


test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('cleanup server upgrade')

-- Test Tarantool 2.1.0 to 2.1.1 migration.
work_dir = 'sql/upgrade/2.1.0/'
test_run:cmd('create server upgrade210 with script="sql/upgrade/upgrade.lua", workdir="' .. work_dir .. '"')
test_run:cmd('start server upgrade210')

test_run:switch('upgrade210')

s = box.space.T5
s ~= nil
i = box.space._index:select(s.id)
i ~= nil
i[1].opts.sql == nil
box.space._space:get(s.id).flags.checks == nil
check = box.space._ck_constraint:select()[1]
check ~= nil
check.name
check.code
s:drop()

test_run:switch('default')
test_run:cmd('stop server upgrade210')
test_run:cmd('cleanup server upgrade210')
