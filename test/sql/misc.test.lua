test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

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
