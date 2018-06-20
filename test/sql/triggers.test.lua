env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- Get invariant part of the tuple; name and opts don't change.
 function immutable_part(data) local r = {} for i, l in pairs(data) do table.insert(r, {l.name, l.opts}) end return r end

--
-- gh-3273: Move Triggers to server
--

box.sql.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
box.sql.execute("CREATE TABLE t2(x INTEGER PRIMARY KEY);")
box.sql.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(1); END; ]])
immutable_part(box.space._trigger:select())

space_id = box.space._space.index["name"]:get('T1').id

-- Checks for LUA tuples.
tuple = {"T1t", space_id, {sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(1); END;"}}
box.space._trigger:insert(tuple)

tuple = {"T1t", space_id, {sql = "CREATE TRIGGER t12t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(1); END;"}}
box.space._trigger:insert(tuple)

tuple = {"T2T", box.space.T1.id + 1, {sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(1); END;"}}
box.space._trigger:insert(tuple)
immutable_part(box.space._trigger:select())

box.sql.execute("DROP TABLE T1;")
immutable_part(box.space._trigger:select())

box.sql.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
box.sql.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(1); END; ]])
immutable_part(box.space._trigger:select())

space_id = box.space._space.index["name"]:get('T1').id

-- Test, didn't trigger t1t degrade.
box.sql.execute("INSERT INTO t1 VALUES(1);")
box.sql.execute("SELECT * FROM t2;")
box.sql.execute("DELETE FROM t2;")


-- Test triggers.
tuple = {"T2T", space_id, {sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(2); END;"}}
_ = box.space._trigger:insert(tuple)
tuple = {"T3T", space_id, {sql = "CREATE TRIGGER t3t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(3); END;"}}
_ = box.space._trigger:insert(tuple)
immutable_part(box.space._trigger:select())
box.sql.execute("INSERT INTO t1 VALUES(2);")
box.sql.execute("SELECT * FROM t2;")
box.sql.execute("DELETE FROM t2;")

-- Test t1t after t2t and t3t drop.
box.sql.execute("DROP TRIGGER T2T;")
_ = box.space._trigger:delete("T3T")
immutable_part(box.space._trigger:select())
box.sql.execute("INSERT INTO t1 VALUES(3);")
box.sql.execute("SELECT * FROM t2;")
box.sql.execute("DELETE FROM t2;")

-- Insert new SQL t2t and t3t.
box.sql.execute([[CREATE TRIGGER t2t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(2); END; ]])
box.sql.execute([[CREATE TRIGGER t3t AFTER INSERT ON t1 BEGIN INSERT INTO t2 VALUES(3); END; ]])
immutable_part(box.space._trigger:select())
box.sql.execute("INSERT INTO t1 VALUES(4);")
box.sql.execute("SELECT * FROM t2;")

-- Clean up.
box.sql.execute("DROP TABLE t1;")
box.sql.execute("DROP TABLE t2;")
immutable_part(box.space._trigger:select())

-- Test target tables restricts.
box.sql.execute("CREATE TABLE t1(a INT PRIMARY KEY,b);")
space_id = box.space.T1.id

tuple = {"T1T", space_id, {sql = [[create trigger t1t instead of update on t1 for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

box.sql.execute("CREATE VIEW V1 AS SELECT * FROM t1;")
space_id = box.space.V1.id

tuple = {"V1T", space_id, {sql = [[create trigger v1t before update on v1 for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

tuple = {"V1T", space_id, {sql = [[create trigger v1t AFTER update on v1 for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

space_id =  box.space._sql_stat1.id
tuple = {"T1T", space_id, {sql = [[create trigger t1t instead of update on "_sql_stat1" for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

box.sql.execute("DROP VIEW V1;")
box.sql.execute("DROP TABLE T1;")
