remote = require('net.box')

box.sql.execute('create table test (id primary key, a float, b text)')
space = box.space.TEST
space:replace{1, 2, '3'}
space:replace{4, 5, '6'}
space:replace{7, 8.5, '9'}
box.sql.execute('select * from test')
box.schema.user.grant('guest','read,write,execute', 'universe')
cn = remote.connect(box.cfg.listen)
cn:ping()

--
-- Static queries, with no parameters.
--

-- Simple select.
cn:execute('select * from test')

-- Operation with rowcount result.
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
-- Parmaeters bindig.
--

cn:execute('select * from test where id = ?', {1})
parameters = {}
parameters[1] = {}
parameters[1][':value'] = 1
cn:execute('select * from test where id = :value', parameters)
cn:execute('select ?, ?, ?', {1, 2, 3})
parameters = {}
parameters[1] = 10
parameters[2] = {}
parameters[2]['@value2'] = 12
parameters[3] = {}
parameters[3][':value1'] = 11
cn:execute('select ?, :value1, @value2', parameters)

parameters = {}
parameters[1] = {}
parameters[1]['$value3'] = 1
parameters[2] = 2
parameters[3] = {}
parameters[3][':value1'] = 3
parameters[4] = 4
parameters[5] = 5
parameters[6] = {}
parameters[6]['@value2'] = 6
cn:execute('select $value3, ?, :value1, ?, ?, @value2, ?, $value3', parameters)

-- Try not-integer types.
msgpack = require('msgpack')
cn:execute('select ?, ?, ?, ?, ?', {'abc', -123.456, msgpack.NULL, true, false})

-- Try to replace '?' in meta with something meaningful.
cn:execute('select ? as kek, ? as kek2', {1, 2})

-- Try to bind not existing name.
parameters = {}
parameters[1] = {}
parameters[1]['name'] = 300
cn:execute('select ? as kek', parameters)

-- Try too many parameters in a statement.
sql = 'select '..string.rep('?, ', box.schema.SQL_BIND_PARAMETER_MAX)..'?'
cn:execute(sql)

-- Try too many parameter values.
sql = 'select ?'
parameters = {}
for i = 1, box.schema.SQL_BIND_PARAMETER_MAX + 1 do parameters[i] = i end
cn:execute(sql, parameters)

--
-- Errors during parameters binding.
--
-- Try value > INT64_MAX. SQLite can't bind it, since it has no
-- suitable method in its bind API.
cn:execute('select ? as big_uint', {0xefffffffffffffff})
-- Bind incorrect parameters.
cn:execute('select ?', { {1, 2, 3} })
parameters = {}
parameters[1] = {}
parameters[1][100] = 200
cn:execute('select ?', parameters)

parameters = {}
parameters[1] = {}
parameters[1][':value'] = {kek = 300}
cn:execute('select :value', parameters)

-- gh-2608 SQL iproto DDL
cn:execute('create table test2(id primary key, a, b, c)')
box.space.TEST2.name
cn:execute('insert into test2 values (1, 1, 1, 1)')
cn:execute('select * from test2')
cn:execute('create index test2_a_b_index on test2(a, b)')
#box.space.TEST2.index
cn:execute('drop table test2')
box.space.TEST2

-- gh-2617 DDL row_count either 0 or 1.

-- Test CREATE [IF NOT EXISTS] TABLE.
cn:execute('create table test3(id primary key, a, b)')
-- Rowcount = 1, although two tuples were created:
-- for _space and for _index.
cn:execute('insert into test3 values (1, 1, 1), (2, 2, 2), (3, 3, 3)')
cn:execute('create table if not exists test3(id primary key)')

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
cn:execute('create trigger trig INSERT ON test3 BEGIN SELECT * FROM test3; END;')
cn:execute('create trigger if not exists trig INSERT ON test3 BEGIN SELECT * FROM test3; END;')
cn:execute('drop trigger trig')
cn:execute('drop trigger if exists trig')

-- Test DROP TABLE [IF EXISTS].
-- Create more indexes, triggers and _truncate tuple.
cn:execute('create index idx1 on test3(a)')
cn:execute('create index idx2 on test3(b)')
box.space.TEST3:truncate()
cn:execute('create trigger trig INSERT ON test3 BEGIN SELECT * FROM test3; END;')
cn:execute('insert into test3 values (1, 1, 1), (2, 2, 2), (3, 3, 3)')
cn:execute('drop table test3')
cn:execute('drop table if exists test3')

-- gh-2602 obuf_alloc breaks the tuple in different slabs
_ = space:replace{1, 1, string.rep('a', 4 * 1024 * 1024)}
res = cn:execute('select * from test')
res.metadata
box.sql.execute('drop table test')
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
box.sql.execute('drop table test')

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
space = nil

-- Cleanup xlog
box.snapshot()

