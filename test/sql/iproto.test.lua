remote = require('net.box')
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.execute('create table test (id int primary key, a NUMBER, b text)')
space = box.space.TEST
space:replace{1, 2, '3'}
space:replace{4, 5, '6'}
space:replace{7, 8.5, '9'}
box.execute('select * from test')
box.schema.user.grant('guest','read,write,execute', 'universe')
box.schema.user.grant('guest', 'create', 'space')
cn = remote.connect(box.cfg.listen)
cn:ping()

--
-- Static queries, with no parameters.
--

-- Simple select.
ret = cn:execute('select * from test')
ret
type(ret.rows[1])

-- Operation with row_count result.
cn:execute('insert into test values (10, 11, NULL)')
cn:execute('delete from test where a = 5')
cn:execute('insert into test values (11, 12, NULL), (12, 12, NULL), (13, 12, NULL)')
cn:execute('delete from test where a = 12')

-- SQL errors.
cn:execute('insert into not_existing_table values ("kek")')
cn:execute('insert qwerty gjsdjq  q  qwd qmq;; q;qwd;')

-- Empty result.
cn:execute('select id as identifier from test where a = 5;')

-- netbox API errors.
cn:execute(100)
cn:execute('select 1', nil, {dry_run = true})

-- Empty request.
cn:execute('')
cn:execute('   ;')

--
-- gh-3467: allow only positive integers under limit clause.
--
cn:execute('select * from test where id = ?', {1})
cn:execute('select * from test limit ?', {2})
cn:execute('select * from test limit ?', {-2})
cn:execute('select * from test limit ?', {2.7})
cn:execute('select * from test limit ?', {'Hello'})

cn:execute('select * from test limit 1 offset ?', {2})
cn:execute('select * from test limit 1 offset ?', {-2})
cn:execute('select * from test limit 1 offset ?', {2.7})
cn:execute('select * from test limit 1 offset ?', {'Hello'})

-- gh-2608 SQL iproto DDL
cn:execute('create table test2(id int primary key, a int, b int, c int)')
box.space.TEST2.name
cn:execute('insert into test2 values (1, 1, 1, 1)')
cn:execute('select * from test2')
cn:execute('create index test2_a_b_index on test2(a, b)')
#box.space.TEST2.index
cn:execute('drop table test2')
box.space.TEST2

-- gh-2617 DDL row_count either 0 or 1.

-- Test CREATE [IF NOT EXISTS] TABLE.
cn:execute('create table test3(id int primary key, a int, b int)')
-- Rowcount = 1, although two tuples were created:
-- for _space and for _index.
cn:execute('insert into test3 values (1, 1, 1), (2, 2, 2), (3, 3, 3)')
cn:execute('create table if not exists test3(id int primary key)')

-- Test CREATE VIEW [IF NOT EXISTS] and
--      DROP   VIEW [IF EXISTS].
cn:execute('create view test3_view(id) as select id from test3')
cn:execute('create view if not exists test3_view(id) as select id from test3')
cn:execute('drop view test3_view')
cn:execute('drop view if exists test3_view')

-- Test CREATE INDEX [IF NOT EXISTS] and
--      DROP   INDEX [IF EXISTS].
cn:execute('create index test3_sec on test3(a, b)')
cn:execute('create index if not exists test3_sec on test3(a, b)')
cn:execute('drop index test3_sec on test3')
cn:execute('drop index if exists test3_sec on test3')

-- Test CREATE TRIGGER [IF NOT EXISTS] and
--      DROP   TRIGGER [IF EXISTS].
cn:execute('CREATE TRIGGER trig INSERT ON test3 FOR EACH ROW BEGIN SELECT * FROM test3; END;')
cn:execute('CREATE TRIGGER if not exists trig INSERT ON test3 FOR EACH ROW BEGIN SELECT * FROM test3; END;')
cn:execute('drop trigger trig')
cn:execute('drop trigger if exists trig')

-- Test DROP TABLE [IF EXISTS].
-- Create more indexes, triggers and _truncate tuple.
cn:execute('create index idx1 on test3(a)')
cn:execute('create index idx2 on test3(b)')
box.space.TEST3:truncate()
cn:execute('CREATE TRIGGER trig INSERT ON test3 FOR EACH ROW BEGIN SELECT * FROM test3; END;')
cn:execute('insert into test3 values (1, 1, 1), (2, 2, 2), (3, 3, 3)')
cn:execute('drop table test3')
cn:execute('drop table if exists test3')

--
-- gh-2948: sql: remove unnecessary templates for binding
-- parameters.
--
cn:execute('select ?1, ?2, ?3', {1, 2, 3})
cn:execute('select $name, $name2', {1, 2})
parameters = {}
parameters[1] = 11
parameters[2] = 22
parameters[3] = 33
cn:execute('select $2, $1, $3', parameters)
cn:execute('select * from test where id = :1', {1})

-- gh-2602 obuf_alloc breaks the tuple in different slabs
_ = space:replace{1, 1, string.rep('a', 4 * 1024 * 1024)}
res = cn:execute('select * from test')
res.metadata
box.execute('drop table test')
cn:close()

--
-- gh-3107: async netbox.
--
cn = remote.connect(box.cfg.listen)

cn:execute('create table test (id integer primary key, a integer, b integer)')
future1 = cn:execute('insert into test values (1, 1, 1)', nil, nil, {is_async = true})
future2 = cn:execute('insert into test values (1, 2, 2)', nil, nil, {is_async = true})
future3 = cn:execute('insert into test values (2, 2, 2), (3, 3, 3)', nil, nil, {is_async = true})
future1:wait_result()
future2:wait_result()
future3:wait_result()
future4 = cn:execute('select * from test', nil, nil, {is_async = true})
future4:wait_result()
cn:close()
box.execute('drop table test')

-- gh-2618 Return generated columns after INSERT in IPROTO.
-- Return all ids generated in current INSERT statement.
box.execute('create table test (id integer primary key autoincrement, a integer)')

cn = remote.connect(box.cfg.listen)
cn:execute('insert into test values (1, 1)')
cn:execute('insert into test values (null, 2)')
cn:execute('update test set a = 11 where id == 1')
cn:execute('insert into test values (100, 1), (null, 1), (120, 1), (null, 1)')
cn:execute('insert into test values (null, 1), (null, 1), (null, 1), (null, 1), (null, 1)')
cn:execute('select * from test')

s = box.schema.create_space('test2', {engine = engine})
sq = box.schema.sequence.create('test2')
pk = s:create_index('pk', {sequence = 'test2'})
function push_id() s:replace{box.NULL} s:replace{box.NULL} end
_ = box.space.TEST:on_replace(push_id)
cn:execute('insert into test values (null, 1)')

box.execute('create table test3 (id int primary key autoincrement)')
box.schema.sequence.alter('TEST3', {min=-10000, step=-10})
cn:execute('insert into TEST3 values (null), (null), (null), (null)')

box.execute('drop table test')
s:drop()
sq:drop()
box.execute('drop table test3')

--
-- Ensure that FK inside CREATE TABLE does not affect row_count.
--
cn:execute('create table test (id integer primary key, a integer)')
cn:execute('create table test2 (id integer primary key, ref integer references test(id))')
cn:execute('drop table test2')

--
-- Ensure that REPLACE is accounted twice in row_count. As delete +
-- insert.
--
cn:execute('insert into test values(1, 1)')
cn:execute('insert or replace into test values(1, 2)')
cn:execute('drop table test')

-- SELECT returns unpacked msgpack.
format = {{name = 'id', type = 'integer'}, {name = 'x', type = 'any'}}
s = box.schema.space.create('test', {format=format})
i1 = s:create_index('i1', {parts = {1, 'int'}})
s:insert({1, {1,2,3}})
s:insert({2, {a = 3}})
cn:execute('select * from "test"')
s:drop()

-- Too many autogenerated ids leads to SEGFAULT.
cn = remote.connect(box.cfg.listen)
box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT)')
for i = 0, 1000 do cn:execute("INSERT INTO t1 VALUES (null)") end
_ = cn:execute("INSERT INTO t1 SELECT NULL from t1")
box.execute('DROP TABLE t1')

cn:close()

-- gh-3832: Some statements do not return column type
box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY)')
cn = remote.connect(box.cfg.listen)

-- PRAGMA:
res = cn:execute("PRAGMA table_info(t1)")
res.metadata

-- EXPLAIN
res = cn:execute("EXPLAIN SELECT 1")
res.metadata
res = cn:execute("EXPLAIN QUERY PLAN SELECT COUNT(*) FROM t1")
res.metadata

-- Make sure that built-in functions have a right returning type.
--
cn:execute("SELECT zeroblob(1);")
-- randomblob() returns different results each time, so check only
-- type in meta.
--
res = cn:execute("SELECT randomblob(1);")
res.metadata
-- Type set during compilation stage, and since min/max are accept
-- arguments of all scalar type, we can't say nothing more than
-- SCALAR.
--
cn:execute("SELECT LEAST(1, 2, 3);")
cn:execute("SELECT GREATEST(1, 2, 3);")

cn:close()
box.execute('DROP TABLE t1')

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.schema.user.revoke('guest', 'create', 'space')
space = nil

--
-- gh-4756: PREPARE and EXECUTE statistics should be present in box.stat()
--
p = box.stat().PREPARE.total
e = box.stat().EXECUTE.total

s = box.prepare([[ SELECT ?; ]])
s:execute({42})
box.execute('SELECT 1;')
res, err = box.unprepare(s)

assert(box.stat().PREPARE.total == p + 1)
assert(box.stat().EXECUTE.total == e + 2)

-- Cleanup xlog
box.snapshot()
