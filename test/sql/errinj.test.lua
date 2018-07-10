remote = require('net.box')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')
errinj = box.error.injection
fiber = require('fiber')

box.sql.execute('create table test (id primary key, a float, b text)')
box.schema.user.grant('guest','read,write,execute', 'universe')
cn = remote.connect(box.cfg.listen)
cn:ping()

-- gh-2601 iproto messages are corrupted
errinj = box.error.injection
fiber = require('fiber')
errinj.set("ERRINJ_WAL_DELAY", true)
insert_res = nil
select_res = nil
function execute_yield() insert_res = cn:execute("insert into test values (100, 1, '1')") end
function execute_notyield() select_res = cn:execute('select 1') end
f1 = fiber.create(execute_yield)
while f1:status() ~= 'suspended' do fiber.sleep(0) end
f2 = fiber.create(execute_notyield)
while f2:status() ~= 'dead' do fiber.sleep(0) end
errinj.set("ERRINJ_WAL_DELAY", false)
while f1:status() ~= 'dead' do fiber.sleep(0) end
insert_res
select_res

cn:close()
box.sql.execute('drop table test')

--
-- gh-3326: after the iproto start using new buffers rotation
-- policy, SQL responses could be corrupted, when DDL/DML is mixed
-- with DQL. Same as gh-3255.
--
box.sql.execute('CREATE TABLE test (id integer primary key)')
cn = remote.connect(box.cfg.listen)

ch = fiber.channel(200)
errinj.set("ERRINJ_IPROTO_TX_DELAY", true)
for i = 1, 100 do fiber.create(function() for j = 1, 10 do cn:execute('REPLACE INTO test VALUES (1)') end ch:put(true) end) end
for i = 1, 100 do fiber.create(function() for j = 1, 10 do cn.space.TEST:get{1} end ch:put(true) end) end
for i = 1, 200 do ch:get() end
errinj.set("ERRINJ_IPROTO_TX_DELAY", false)

box.sql.execute('DROP TABLE test')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

----
---- gh-3273: Move SQL TRIGGERs into server.
----
box.sql.execute("CREATE TABLE t1(id INTEGER PRIMARY KEY, a INTEGER);");
box.sql.execute("CREATE TABLE t2(id INTEGER PRIMARY KEY, a INTEGER);");
box.error.injection.set("ERRINJ_WAL_IO", true)
box.sql.execute("CREATE TRIGGER t1t INSERT ON t1 BEGIN INSERT INTO t2 VALUES (1, 1); END;")
box.sql.execute("CREATE INDEX t1a ON t1(a);")
box.error.injection.set("ERRINJ_WAL_IO", false)
box.sql.execute("CREATE TRIGGER t1t INSERT ON t1 BEGIN INSERT INTO t2 VALUES (1, 1); END;")
box.sql.execute("INSERT INTO t1 VALUES (3, 3);")
box.sql.execute("SELECT * from t1");
box.sql.execute("SELECT * from t2");
box.error.injection.set("ERRINJ_WAL_IO", true)
box.sql.execute("DROP TRIGGER t1t;")
box.error.injection.set("ERRINJ_WAL_IO", false)
box.sql.execute("DELETE FROM t1;")
box.sql.execute("DELETE FROM t2;")
box.sql.execute("INSERT INTO t1 VALUES (3, 3);")
box.sql.execute("SELECT * from t1");
box.sql.execute("SELECT * from t2");
box.sql.execute("DROP TABLE t1;")
box.sql.execute("DROP TABLE t2;")

-- Tests which are aimed at verifying work of commit/rollback
-- triggers on _fk_constraint space.
--
box.sql.execute("CREATE TABLE t3 (id PRIMARY KEY, a REFERENCES t3, b INT UNIQUE);")
t = box.space._fk_constraint:select{}[1]:totable()
errinj = box.error.injection
errinj.set("ERRINJ_WAL_IO", true)
-- Make constraint reference B field instead of id.
t[9] = {2}
box.space._fk_constraint:replace(t)
errinj.set("ERRINJ_WAL_IO", false)
box.sql.execute("INSERT INTO t3 VALUES (1, 2, 2);")
errinj.set("ERRINJ_WAL_IO", true)
box.sql.execute("ALTER TABLE t3 ADD CONSTRAINT fk1 FOREIGN KEY (b) REFERENCES t3;")
errinj.set("ERRINJ_WAL_IO", false)
box.sql.execute("INSERT INTO t3 VALUES(1, 1, 3);")
box.sql.execute("DELETE FROM t3;")
box.snapshot()
box.sql.execute("ALTER TABLE t3 ADD CONSTRAINT fk1 FOREIGN KEY (b) REFERENCES t3;")
box.sql.execute("INSERT INTO t3 VALUES(1, 1, 3);")
errinj.set("ERRINJ_WAL_IO", true)
box.sql.execute("ALTER TABLE t3 DROP CONSTRAINT fk1;")
box.sql.execute("INSERT INTO t3 VALUES(1, 1, 3);")
errinj.set("ERRINJ_WAL_IO", false)
box.sql.execute("DROP TABLE t3;")
