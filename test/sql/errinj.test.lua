remote = require('net.box')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})
errinj = box.error.injection
fiber = require('fiber')

-- gh-3924 Check that tuple_formats of ephemeral spaces are
-- reused.
box.execute("CREATE TABLE t4 (id INTEGER PRIMARY KEY, a INTEGER);")
box.execute("INSERT INTO t4 VALUES (1,1)")
box.execute("INSERT INTO t4 VALUES (2,1)")
box.execute("INSERT INTO t4 VALUES (3,2)")
errinj.set('ERRINJ_TUPLE_FORMAT_COUNT', 200)
errinj.set('ERRINJ_MEMTX_DELAY_GC', true)
for i = 1, 201 do box.execute("SELECT DISTINCT a FROM t4") end
errinj.set('ERRINJ_MEMTX_DELAY_GC', false)
errinj.set('ERRINJ_TUPLE_FORMAT_COUNT', -1)
box.execute('DROP TABLE t4')

box.execute('create table test (id int primary key, a NUMBER, b text)')
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
box.execute('drop table test')

--
-- gh-3326: after the iproto start using new buffers rotation
-- policy, SQL responses could be corrupted, when DDL/DML is mixed
-- with DQL. Same as gh-3255.
--
box.execute('CREATE TABLE test (id integer primary key)')
cn = remote.connect(box.cfg.listen)

ch = fiber.channel(200)
errinj.set("ERRINJ_IPROTO_TX_DELAY", true)
for i = 1, 100 do fiber.create(function() for j = 1, 10 do cn:execute('REPLACE INTO test VALUES (1)') end ch:put(true) end) end
for i = 1, 100 do fiber.create(function() for j = 1, 10 do cn.space.TEST:get{1} end ch:put(true) end) end
for i = 1, 200 do ch:get() end
errinj.set("ERRINJ_IPROTO_TX_DELAY", false)

box.execute('DROP TABLE test')
box.schema.user.revoke('guest', 'read,write,execute', 'universe')

----
---- gh-3273: Move SQL TRIGGERs into server.
----
box.execute("CREATE TABLE t1(id INTEGER PRIMARY KEY, a INTEGER);");
box.execute("CREATE TABLE t2(id INTEGER PRIMARY KEY, a INTEGER);");
box.error.injection.set("ERRINJ_WAL_IO", true)
box.execute("CREATE TRIGGER t1t INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES (1, 1); END;")
box.execute("CREATE INDEX t1a ON t1(a);")
box.error.injection.set("ERRINJ_WAL_IO", false)
box.execute("CREATE TRIGGER t1t INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES (1, 1); END;")
box.execute("INSERT INTO t1 VALUES (3, 3);")
box.execute("SELECT * from t1");
box.execute("SELECT * from t2");
box.error.injection.set("ERRINJ_WAL_IO", true)
t = box.space._trigger:get('T1T')
t_new = t:totable()
t_new[3]['sql'] = 'CREATE TRIGGER t1t INSERT ON t1 FOR EACH ROW BEGIN INSERT INTO t2 VALUES (2, 2); END;'
_ = box.space._trigger:replace(t, t_new)
box.error.injection.set("ERRINJ_WAL_IO", false)
_ = box.space._trigger:replace(t, t_new)
box.error.injection.set("ERRINJ_WAL_IO", true)
box.execute("DROP TRIGGER t1t;")
box.error.injection.set("ERRINJ_WAL_IO", false)
box.execute("DELETE FROM t1;")
box.execute("DELETE FROM t2;")
box.execute("INSERT INTO t1 VALUES (3, 3);")
box.execute("SELECT * from t1");
box.execute("SELECT * from t2");
box.execute("DROP TABLE t1;")
box.execute("DROP TABLE t2;")

-- Tests which are aimed at verifying work of commit/rollback
-- triggers on _fk_constraint space.
--
box.execute("CREATE TABLE t3 (id NUMBER PRIMARY KEY, a INT REFERENCES t3, b INT UNIQUE);")
t = box.space._fk_constraint:select{}[1]:totable()
errinj = box.error.injection
errinj.set("ERRINJ_WAL_IO", true)
-- Make constraint reference B field instead of id.
t[9] = {2}
box.space._fk_constraint:replace(t)
errinj.set("ERRINJ_WAL_IO", false)
box.execute("INSERT INTO t3 VALUES (1, 2, 2);")
errinj.set("ERRINJ_WAL_IO", true)
box.execute("ALTER TABLE t3 ADD CONSTRAINT fk1 FOREIGN KEY (b) REFERENCES t3;")
errinj.set("ERRINJ_WAL_IO", false)
box.execute("INSERT INTO t3 VALUES(1, 1, 3);")
box.execute("DELETE FROM t3;")
box.snapshot()
box.execute("ALTER TABLE t3 ADD CONSTRAINT fk1 FOREIGN KEY (b) REFERENCES t3;")
box.execute("INSERT INTO t3 VALUES(1, 1, 3);")
errinj.set("ERRINJ_WAL_IO", true)
box.execute("ALTER TABLE t3 DROP CONSTRAINT fk1;")
box.execute("INSERT INTO t3 VALUES(1, 1, 3);")
errinj.set("ERRINJ_WAL_IO", false)
box.execute("DROP TABLE t3;")

--
-- Tests which are aimed at verifying work of commit/rollback
-- triggers on _ck_constraint space.
--
s = box.schema.space.create('test', {format = {{name = 'X', type = 'unsigned'}}})
pk = box.space.test:create_index('pk')

errinj.set("ERRINJ_WAL_IO", true)
_ = box.space._ck_constraint:insert({s.id, 'CK_CONSTRAINT_01', false, 'SQL', 'X<5', true})
errinj.set("ERRINJ_WAL_IO", false)
_ = box.space._ck_constraint:insert({s.id, 'CK_CONSTRAINT_01', false, 'SQL', 'X<5', true})
box.execute("INSERT INTO \"test\" VALUES(5);")
errinj.set("ERRINJ_WAL_IO", true)
_ = box.space._ck_constraint:replace({s.id, 'CK_CONSTRAINT_01', false, 'SQL', 'X<=5', true})
errinj.set("ERRINJ_WAL_IO", false)
_ = box.space._ck_constraint:replace({s.id, 'CK_CONSTRAINT_01', false, 'SQL', 'X<=5', true})
box.execute("INSERT INTO \"test\" VALUES(5);")
errinj.set("ERRINJ_WAL_IO", true)
_ = box.space._ck_constraint:delete({s.id, 'CK_CONSTRAINT_01'})
box.execute("INSERT INTO \"test\" VALUES(6);")
errinj.set("ERRINJ_WAL_IO", false)
_ = box.space._ck_constraint:delete({s.id, 'CK_CONSTRAINT_01'})
box.execute("INSERT INTO \"test\" VALUES(6);")
s:drop()

--
-- Test that failed space alter doesn't harm ck constraints
--
s = box.schema.create_space('test')
_ = s:create_index('pk')
s:format({{name='X', type='integer'}, {name='Y', type='integer'}})
_ = box.space._ck_constraint:insert({s.id, 'XlessY', false, 'SQL', 'X < Y', true})
_ = box.space._ck_constraint:insert({s.id, 'Xgreater10', false, 'SQL', 'X > 10', true})
box.execute("INSERT INTO \"test\" VALUES(1, 2);")
box.execute("INSERT INTO \"test\" VALUES(20, 10);")
box.execute("INSERT INTO \"test\" VALUES(20, 100);")
s:truncate()
errinj.set("ERRINJ_WAL_IO", true)
s:format({{name='Y', type='integer'}, {name='X', type='integer'}})
errinj.set("ERRINJ_WAL_IO", false)
box.execute("INSERT INTO \"test\" VALUES(1, 2);")
box.execute("INSERT INTO \"test\" VALUES(20, 10);")
box.execute("INSERT INTO \"test\" VALUES(20, 100);")
s:drop()
