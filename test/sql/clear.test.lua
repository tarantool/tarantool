test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- box.cfg()

-- create space
box.execute("CREATE TABLE zoobar (c1 INT, c2 INT PRIMARY KEY, c3 TEXT, c4 INT)")
box.execute("CREATE UNIQUE INDEX zoobar2 ON zoobar(c1, c4)")

-- Seed entry
for i=1, 100 do box.execute(string.format("INSERT INTO zoobar VALUES (%d, %d, 'c3', 444)", i+i, i)) end

-- Check table is not empty
box.execute("SELECT * FROM zoobar")

-- Do clean up
box.execute("DELETE FROM zoobar")

-- Make sure table is empty
box.execute("SELECT * from zoobar")

-- Cleanup
box.execute("DROP INDEX zoobar2 ON zoobar")
box.execute("DROP TABLE zoobar")

-- Debug
-- require("console").start()

--
-- gh-4183: Check if there is a garbage in case of failure to
-- create a constraint, when more than one constraint of the same
-- type is created with the same name and in the same
-- CREATE TABLE statement.
--
box.execute("CREATE TABLE t1(id INT PRIMARY KEY, CONSTRAINT ck1 CHECK(id > 0), CONSTRAINT ck1 CHECK(id < 0));")
box.space.t1
box.space._ck_constraint:select()

box.execute("CREATE TABLE t2(id INT PRIMARY KEY, CONSTRAINT fk1 FOREIGN KEY(id) REFERENCES t2, CONSTRAINT fk1 FOREIGN KEY(id) REFERENCES t2);")
box.space.t2
box.space._fk_constraint:select()

--
-- Make sure that keys for tuples inserted into system spaces were
-- not stored in temporary cells.
--
box.execute("CREATE TABLE t3(id INT PRIMARY KEY, CONSTRAINT ck1 CHECK(id > 0), CONSTRAINT ck1 FOREIGN KEY(id) REFERENCES t3, CONSTRAINT fk1 FOREIGN KEY(id) REFERENCES t3, CONSTRAINT ck1 CHECK(id < 0));")
box.space.t1
box.space._ck_constraint:select()
box.space._fk_constraint:select()
