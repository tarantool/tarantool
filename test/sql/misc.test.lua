-- Forbid multistatement queries.
box.sql.execute('select 1;')
box.sql.execute('select 1; select 2;')
box.sql.execute('create table t1 (id primary key); select 100;')
box.space.t1 == nil
