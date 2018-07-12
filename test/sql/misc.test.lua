test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- Forbid multistatement queries.
box.sql.execute('select 1;')
box.sql.execute('select 1; select 2;')
box.sql.execute('create table t1 (id INT primary key); select 100;')
box.space.t1 == nil
box.sql.execute(';')
box.sql.execute('')
box.sql.execute('     ;')
box.sql.execute('\n\n\n\t\t\t   ')
