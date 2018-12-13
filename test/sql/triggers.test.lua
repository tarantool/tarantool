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
box.sql.execute("CREATE TABLE t1(a INT PRIMARY KEY,b INT);")
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

--
-- gh-3531: Assertion with trigger and two storage engines
--
-- Case 1: Src 'vinyl' table; Dst 'memtx' table
box.sql.execute("PRAGMA sql_default_engine ('vinyl');")
box.sql.execute("CREATE TABLE m (s0 INT PRIMARY KEY, s1 TEXT UNIQUE);")
box.sql.execute("CREATE TRIGGER m1 BEFORE UPDATE ON m FOR EACH ROW BEGIN UPDATE n SET s2 = DATETIME('now'); END;")
box.sql.execute("PRAGMA sql_default_engine('memtx');")
box.sql.execute("CREATE TABLE n (s0 INT PRIMARY KEY, s1 TEXT UNIQUE, s2 REAL);")
box.sql.execute("INSERT INTO m VALUES (0, '0');")
box.sql.execute("INSERT INTO n VALUES (0, '',null);")
box.sql.execute("UPDATE m SET s1 = 'The Rain In Spain';")

-- ANALYZE operates with _sql_stat{1,4} tables should work
box.sql.execute("ANALYZE m;")
box.sql.execute("DROP TABLE m;")
box.sql.execute("DROP TABLE n;")


-- Case 2: Src 'memtx' table; Dst 'vinyl' table
box.sql.execute("PRAGMA sql_default_engine ('memtx');")
box.sql.execute("CREATE TABLE m (s0 INT PRIMARY KEY, s1 TEXT UNIQUE);")
box.sql.execute("CREATE TRIGGER m1 BEFORE UPDATE ON m FOR EACH ROW BEGIN UPDATE n SET s2 = DATETIME('now'); END;")
box.sql.execute("PRAGMA sql_default_engine('vinyl');")
box.sql.execute("CREATE TABLE n (s0 INT PRIMARY KEY, s1 TEXT UNIQUE, s2 REAL);")
box.sql.execute("INSERT INTO m VALUES (0, '0');")
box.sql.execute("INSERT INTO n VALUES (0, '',null);")
box.sql.execute("UPDATE m SET s1 = 'The Rain In Spain';")

-- ANALYZE operates with _sql_stat{1,4} tables should work
box.sql.execute("ANALYZE n;")
box.sql.execute("DROP TABLE m;")
box.sql.execute("DROP TABLE n;")

-- Test SQL Transaction with LUA
box.sql.execute("PRAGMA sql_default_engine ('memtx');")
box.sql.execute("CREATE TABLE test (id INT PRIMARY KEY)")
box.sql.execute("PRAGMA sql_default_engine='vinyl'")
box.sql.execute("CREATE TABLE test2 (id INT PRIMARY KEY)")
box.sql.execute("INSERT INTO test2 VALUES (2)")
box.sql.execute("START TRANSACTION")
box.sql.execute("INSERT INTO test VALUES (1)")
box.sql.execute("SELECT * FROM test2")
box.sql.execute("ROLLBACK;")
box.sql.execute("DROP TABLE test;")
box.sql.execute("DROP TABLE test2;")

--
-- gh-3536: Some triggers cause error messages and/or half-finished updates
--
box.sql.execute("CREATE TABLE t (s1 INT, s2 INT, s3 INT, s4 INT PRIMARY KEY);")
box.sql.execute("CREATE VIEW v AS SELECT s1, s2 FROM t;")
box.sql.execute("CREATE TRIGGER tv INSTEAD OF UPDATE ON v BEGIN UPDATE t SET s3 = new.s1 WHERE s1 = old.s1; END;")
box.sql.execute("INSERT INTO t VALUES (1,1,1,1);")
box.sql.execute("UPDATE v SET s2 = s1 + 1;")
box.sql.execute("UPDATE v SET s1 = s1 + 5;")
box.sql.execute("SELECT * FROM t;")
box.sql.execute("DROP VIEW v;")
box.sql.execute("DROP TABLE t;")

--
-- gh-3653: Dissallow bindings for DDL
--
box.sql.execute("CREATE TABLE t1(a INT PRIMARY KEY, b INT);")
space_id = box.space.T1.id
box.sql.execute("CREATE TRIGGER tr1 AFTER INSERT ON t1 WHEN new.a = ? BEGIN SELECT 1; END;")
tuple = {"TR1", space_id, {sql = [[CREATE TRIGGER tr1 AFTER INSERT ON t1 WHEN new.a = ? BEGIN SELECT 1; END;]]}}
box.space._trigger:insert(tuple)
box.sql.execute("DROP TABLE t1;")
