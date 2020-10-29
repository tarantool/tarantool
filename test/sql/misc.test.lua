test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- Forbid multistatement queries.
box.execute('select 1;')
box.execute('select 1; select 2;')
box.execute('create table t1 (id INT primary key); select 100;')
box.space.t1 == nil
box.execute(';')
box.execute('')
box.execute('     ;')
box.execute('\n\n\n\t\t\t   ')

-- gh-3820: only table constraints can have a name.
--
box.execute('CREATE TABLE test (id INTEGER PRIMARY KEY, b INTEGER CONSTRAINT c1 NULL)')
box.execute('CREATE TABLE test (id INTEGER PRIMARY KEY, b INTEGER CONSTRAINT c1 DEFAULT 300)')
box.execute('CREATE TABLE test (id INTEGER PRIMARY KEY, b TEXT CONSTRAINT c1 COLLATE "binary")')

-- Make sure that type of literals in meta complies with its real
-- type. For instance, typeof(0.5) is number, not integer.
--
box.execute('SELECT 1;')
box.execute('SELECT 1.5;')
box.execute('SELECT 1.0;')
box.execute('SELECT \'abc\';')
box.execute('SELECT X\'4D6564766564\'')

--
-- gh-4139: assertion when reading a temporary space.
--
format = {{name = 'id', type = 'integer'}}
s = box.schema.space.create('s',{format=format, temporary=true})
i = s:create_index('i')
box.execute('select * from "s"')
s:drop()

--
-- gh-4267: Full power of vdbe_field_ref
-- Tarantool's SQL internally stores data offset for all acceded
-- fields. It also keeps a bitmask of size 64 with all initialized
-- slots in actual state to find the nearest left field really
-- fast and parse tuple from that position. For fieldno >= 64
-- bitmask is not applicable, so it scans data offsets area in
-- a cycle.
--
-- The test below covers a case when this optimisation doesn't
-- work and the second lookup require parsing tuple from
-- beginning.
---
format = {}
t = {}
for i = 1, 70 do                                                \
        format[i] = {name = 'FIELD'..i, type = 'unsigned'}      \
        t[i] = i                                                \
end
s = box.schema.create_space('TEST', {format = format})
pk = s:create_index('pk', {parts = {70}})
s:insert(t)
box.execute('SELECT field70, field64 FROM test')

-- In the case below described optimization works fine.
pk:alter({parts = {66}})
box.execute('SELECT field66, field68, field70 FROM test')
box.space.TEST:drop()

-- gh-4933: Make sure that autoindex optimization is used.
box.execute('CREATE TABLE t1(i INT PRIMARY KEY, a INT);')
box.execute('CREATE TABLE t2(i INT PRIMARY KEY, b INT);')
for i = 1, 10240 do\
	box.execute('INSERT INTO t1 VALUES ($1, $1);', {i})\
	box.execute('INSERT INTO t2 VALUES ($1, $1);', {i})\
end
box.execute('EXPLAIN QUERY PLAN SELECT a, b FROM t1, t2 WHERE a = b;')

-- gh-5592: Make sure that diag is not changed with the correct query.
box.execute('SELECT a;')
diag = box.error.last()
box.execute('SELECT * FROM (VALUES(true));')
diag == box.error.last()

-- exclude_null + SQL correctness
box.execute([[CREATE TABLE j (s1 INT PRIMARY KEY, s2 STRING, s3 VARBINARY)]])
s = box.space.J
i = box.space.J:create_index('I3',{parts={2,'string', exclude_null=true}})
box.execute([[INSERT INTO j VALUES (1,NULL,NULL), (2,'',X'00');]])

box.execute([[SELECT * FROM j;]])
box.execute([[SELECT * FROM j INDEXED BY I3;]])

box.execute([[SELECT COUNT(*) FROM j GROUP BY s2;]])
box.execute([[SELECT COUNT(*) FROM j INDEXED BY I3;]])

box.execute([[UPDATE j INDEXED BY i3 SET s2 = NULL;]])
box.execute([[INSERT INTO j VALUES (3, 'a', X'33');]])

box.execute([[SELECT * FROM j;]])
box.execute([[SELECT * FROM j INDEXED BY I3;]])

box.execute([[UPDATE j INDEXED BY i3 SET s3 = NULL;]])
s:select{}

s:drop()
