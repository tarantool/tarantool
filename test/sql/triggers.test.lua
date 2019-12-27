env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- Get invariant part of the tuple; name and opts don't change.
 function immutable_part(data) local r = {} for i, l in pairs(data) do table.insert(r, {l.name, l.opts}) end return r end

--
-- gh-3273: Move Triggers to server
--

box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
box.execute("CREATE TABLE t2(x INTEGER PRIMARY KEY);")
box.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END; ]])
immutable_part(box.space._trigger:select())

space_id = box.space._space.index["name"]:get('T1').id

-- Checks for LUA tuples.
tuple = {"T1t", space_id, {sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"}}
box.space._trigger:insert(tuple)

tuple = {"T1t", space_id, {sql = "CREATE TRIGGER t12t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"}}
box.space._trigger:insert(tuple)

tuple = {"T2T", box.space.T1.id + 1, {sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"}}
box.space._trigger:insert(tuple)
immutable_part(box.space._trigger:select())

box.execute("DROP TABLE T1;")
immutable_part(box.space._trigger:select())

box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
box.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END; ]])
immutable_part(box.space._trigger:select())

space_id = box.space._space.index["name"]:get('T1').id

-- Test, didn't trigger t1t degrade.
box.execute("INSERT INTO t1 VALUES(1);")
box.execute("SELECT * FROM t2;")
box.execute("DELETE FROM t2;")


-- Test triggers.
tuple = {"T2T", space_id, {sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(2); END;"}}
_ = box.space._trigger:insert(tuple)
tuple = {"T3T", space_id, {sql = "CREATE TRIGGER t3t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(3); END;"}}
_ = box.space._trigger:insert(tuple)
immutable_part(box.space._trigger:select())
box.execute("INSERT INTO t1 VALUES(2);")
box.execute("SELECT * FROM t2;")
box.execute("DELETE FROM t2;")

-- Test t1t after t2t and t3t drop.
box.execute("DROP TRIGGER T2T;")
_ = box.space._trigger:delete("T3T")
immutable_part(box.space._trigger:select())
box.execute("INSERT INTO t1 VALUES(3);")
box.execute("SELECT * FROM t2;")
box.execute("DELETE FROM t2;")

-- Insert new SQL t2t and t3t.
box.execute([[CREATE TRIGGER t2t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(2); END; ]])
box.execute([[CREATE TRIGGER t3t AFTER INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES(3); END; ]])
immutable_part(box.space._trigger:select())
box.execute("INSERT INTO t1 VALUES(4);")
box.execute("SELECT * FROM t2;")

-- Clean up.
box.execute("DROP TABLE t1;")
box.execute("DROP TABLE t2;")
immutable_part(box.space._trigger:select())

-- Test target tables restricts.
box.execute("CREATE TABLE t1(a INT PRIMARY KEY,b INT);")
space_id = box.space.T1.id

tuple = {"T1T", space_id, {sql = [[create trigger t1t instead of update on t1 for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

box.execute("CREATE VIEW V1 AS SELECT * FROM t1;")
space_id = box.space.V1.id

tuple = {"V1T", space_id, {sql = [[create trigger v1t before update on v1 for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

tuple = {"V1T", space_id, {sql = [[create trigger v1t AFTER update on v1 for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

space_id =  box.space._fk_constraint.id
tuple = {"T1T", space_id, {sql = [[create trigger t1t instead of update on "_fk_constraint" for each row begin delete from t1 WHERE a=old.a+2; end;]]}}
box.space._trigger:insert(tuple)

box.execute("DROP VIEW V1;")
box.execute("DROP TABLE T1;")

--
-- gh-3531: Assertion with trigger and two storage engines
--
-- Case 1: Src 'vinyl' table; Dst 'memtx' table
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'vinyl'}})
box.execute("CREATE TABLE m (s0 INT PRIMARY KEY, s1 TEXT UNIQUE);")
box.execute("CREATE TRIGGER m1 BEFORE UPDATE ON m FOR EACH ROW BEGIN UPDATE n SET s2 = 'now'; END;")
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'memtx'}})
box.execute("CREATE TABLE n (s0 INT PRIMARY KEY, s1 TEXT UNIQUE, s2 NUMBER);")
box.execute("INSERT INTO m VALUES (0, '0');")
box.execute("INSERT INTO n VALUES (0, '',null);")
box.execute("UPDATE m SET s1 = 'The Rain In Spain';")

-- ANALYZE banned in gh-4069
-- box.sql.execute("ANALYZE m;")
box.execute("DROP TABLE m;")
box.execute("DROP TABLE n;")


-- Case 2: Src 'memtx' table; Dst 'vinyl' table
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'memtx'}})
box.execute("CREATE TABLE m (s0 INT PRIMARY KEY, s1 TEXT UNIQUE);")
box.execute("CREATE TRIGGER m1 BEFORE UPDATE ON m FOR EACH ROW BEGIN UPDATE n SET s2 = 'now'; END;")
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'vinyl'}})
box.execute("CREATE TABLE n (s0 INT PRIMARY KEY, s1 TEXT UNIQUE, s2 NUMBER);")
box.execute("INSERT INTO m VALUES (0, '0');")
box.execute("INSERT INTO n VALUES (0, '',null);")
box.execute("UPDATE m SET s1 = 'The Rain In Spain';")

-- ANALYZE banned in gh-4069
-- box.sql.execute("ANALYZE n;")
box.execute("DROP TABLE m;")
box.execute("DROP TABLE n;")

-- Test SQL Transaction with LUA
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'memtx'}})
box.execute("CREATE TABLE test (id INT PRIMARY KEY)")
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'vinyl'}})
box.execute("CREATE TABLE test2 (id INT PRIMARY KEY)")
box.execute("INSERT INTO test2 VALUES (2)")
box.execute("START TRANSACTION")
box.execute("INSERT INTO test VALUES (1)")
box.execute("SELECT * FROM test2")
box.execute("ROLLBACK;")
box.execute("DROP TABLE test;")
box.execute("DROP TABLE test2;")

--
-- gh-3536: Some triggers cause error messages and/or half-finished updates
--
box.execute("CREATE TABLE t (s1 INT, s2 INT, s3 INT, s4 INT PRIMARY KEY);")
box.execute("CREATE VIEW v AS SELECT s1, s2 FROM t;")
box.execute("CREATE TRIGGER tv INSTEAD OF UPDATE ON v FOR EACH ROW BEGIN UPDATE t SET s3 = new.s1 WHERE s1 = old.s1; END;")
box.execute("INSERT INTO t VALUES (1,1,1,1);")
box.execute("UPDATE v SET s2 = s1 + 1;")
box.execute("UPDATE v SET s1 = s1 + 5;")
box.execute("SELECT * FROM t;")
box.execute("DROP VIEW v;")
box.execute("DROP TABLE t;")

--
-- gh-3653: Dissallow bindings for DDL
--
box.execute("CREATE TABLE t1(a INT PRIMARY KEY, b INT);")
space_id = box.space.T1.id
box.execute("CREATE TRIGGER tr1 AFTER INSERT ON t1 FOR EACH ROW WHEN new.a = ? BEGIN SELECT 1; END;")
tuple = {"TR1", space_id, {sql = [[CREATE TRIGGER tr1 AFTER INSERT ON t1 FOR EACH ROW WHEN new.a = ? BEGIN SELECT 1; END;]]}}
box.space._trigger:insert(tuple)
box.execute("DROP TABLE t1;")

-- 
-- Check that FOR EACH ROW clause is moandatory
--
box.execute("CREATE TABLE t1(a INT PRIMARY KEY, b INT);")
space_id = box.space.T1.id
box.execute("CREATE TRIGGER tr1 AFTER INSERT ON t1 BEGIN ; END;")
box.execute("DROP TABLE t1;")

--
-- gh-3570: Use box_space_id_by_name() instead of schema_find_id()
-- in SQL
--
box.schema.user.create('tester')
box.schema.user.grant('tester','read,write,create,execute', 'space', '_trigger')
box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY AUTOINCREMENT);")
box.session.su('tester')
--
-- Ensure that the CREATE TRIGGER statement cannot be executed if
-- the user does not have enough rights. In this case, the user
-- does not have rights to read from _space.
--
box.execute([[CREATE TRIGGER r1 AFTER INSERT ON t1 FOR EACH ROW BEGIN SELECT 1; END; ]])
box.session.su('admin')
box.schema.user.drop('tester')
box.execute("DROP TABLE t1;")

--
-- gh-4188: make sure that the identifiers that were generated
-- during the INSERT performed by the triggers are not returned.
--
box.execute('CREATE TABLE t1 (i INT PRIMARY KEY AUTOINCREMENT);')
box.execute('CREATE TABLE t2 (i INT PRIMARY KEY AUTOINCREMENT);')
box.execute('CREATE TABLE t3 (i INT PRIMARY KEY AUTOINCREMENT);')

box.execute('CREATE TRIGGER r1 AFTER INSERT ON t2 FOR EACH ROW BEGIN INSERT INTO t1 VALUES (null); END')
box.execute('INSERT INTO t1 VALUES (100);')
box.execute('INSERT INTO t2 VALUES (NULL), (NULL), (NULL);')
box.execute('SELECT * FROM t1;')
box.execute('SELECT * FROM t2;')

box.execute('CREATE TRIGGER r2 AFTER INSERT ON t3 FOR EACH ROW BEGIN INSERT INTO t2 VALUES (null); END')
box.execute('INSERT INTO t3 VALUES (NULL), (NULL), (NULL);')
box.execute('SELECT * FROM t1;')
box.execute('SELECT * FROM t2;')
box.execute('SELECT * FROM t3;')

box.execute('DROP TABLE t1;')
box.execute('DROP TABLE t2;')
box.execute('DROP TABLE t3;')
