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

-- gh-5592: Make sure that diag is not changed with the correct query.
box.execute('SELECT a;')
diag = box.error.last()
box.execute('SELECT * FROM (VALUES(true));')
diag == box.error.last()
