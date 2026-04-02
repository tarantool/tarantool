local server = require('luatest.server')
local t = require('luatest')

local g = t.group("iproto", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_iproto = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE test (id INT PRIMARY KEY, a NUMBER, b text);')
        local space = box.space.test
        space:replace{1, 2, '3'}
        space:replace{4, 5, '6'}
        space:replace{7, 8.5, '9'}
        local exp = {
            {1, 2, '3'},
            {4, 5, '6'},
            {7, 8.5, '9'},
        }
        local res = box.execute('SELECT * FROM SEQSCAN test;')
        t.assert_equals(res.rows, exp)

        box.schema.user.create('test_user', {password = 'test'})
        box.schema.user.grant('test_user','read,write,execute', 'universe')
        box.schema.user.grant('test_user', 'create', 'space')
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        t.assert_equals(cn:ping(), true)
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])

        --
        -- Static queries, with no parameters.
        --

        -- Simple select.
        res = cn:execute('SELECT * FROM test;')
        t.assert_equals(res.rows, exp)
        t.assert_equals(type(res.rows[1]), 'cdata')

        -- Operation with row_count result.
        cn:execute('INSERT INTO test VALUES (10, 11, NULL);')
        cn:execute('DELETE FROM test WHERE a = 5;')
        local sql = [[INSERT INTO test VALUES
                      (11, 12, NULL), (12, 12, NULL), (13, 12, NULL);]]
        cn:execute(sql)
        cn:execute('DELETE FROM test WHERE a = 12;')

        -- SQL errors.
        local exp_err = "Space 'not_existing_table' does not exist"
        local sql = 'INSERT INTO not_existing_table VALUES ("kek");'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql)
                                          end)

        exp_err = "Syntax error at line 1 near 'qwerty'"
        sql = 'INSERT qwerty gjsdjq  q  qwd qmq;; q;qwd;'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql)
                                          end)

        -- Empty result.
        res = cn:execute('SELECT id AS IDENTIFIER FROM test WHERE a = 5;')
        t.assert_equals(res.rows, {})

        -- netbox API errors.
        exp_err = 'Prepared statement with id 100 does not exist'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(100)
                                          end)

        exp_err = 'execute does not support options'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(
                                                'SELECT 1',
                                                nil,
                                                {dry_run = true}
                                            )
                                        end)

        -- Empty request.
        exp_err = 'Failed to execute an empty SQL statement'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute('')
                                          end)
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute('   ;')
                                          end)
    end)
end

--
-- gh-3467: allow only positive integers under limit clause.
g.test_3467_allow_only_positive_integers = function(cg)
    cg.server:exec(function()
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])
        local res = cn:execute('SELECT * FROM test WHERE id = ?;', {1})
        t.assert_equals(res.rows, {{1, 2, '3'}})

        local exp = {
            {1, 2, '3'},
            {7, 8.5, '9'},
        }
        res = cn:execute('SELECT * FROM test LIMIT ?;', {2})
        t.assert_equals(res.rows, exp)

        local exp_err = 'Failed to execute SQL statement: '..
                        'Only positive integers are allowed in the '..
                        'LIMIT clause'
        local sql = 'SELECT * FROM test LIMIT ?;'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {-2})
                                          end)
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {2.7})
                                          end)
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {'Hello'})
                                          end)

        res = cn:execute('SELECT * FROM test LIMIT 1 OFFSET ?;', {2})
        t.assert_equals(res.rows, {{10, 11, nil}})

        exp_err = 'Failed to execute SQL statement: '..
                  'Only positive integers are allowed in the '..
                  'OFFSET clause'
        sql = 'SELECT * FROM test LIMIT 1 OFFSET ?;'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {-2})
                                          end)
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {2.7})
                                          end)
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {'Hello'})
                                          end)
    end)
end

-- gh-2608 SQL iproto DDL
g.test_2608_SQL_iproto_DDL = function(cg)
    cg.server:exec(function()
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[CREATE TABLE test2
                      (id INT PRIMARY KEY, a INT, b INT, c INT);]]
        cn:execute(sql)
        t.assert_equals(box.space.test2.name, 'test2')
        cn:execute('INSERT INTO test2 VALUES (1, 1, 1, 1);')

        local res = cn:execute('SELECT * FROM test2;')
        t.assert_equals(res.rows, {{1, 1, 1, 1}})
        cn:execute('CREATE INDEX test2_a_b_index ON test2(a, b);')
        t.assert_equals(#box.space.test2.index, 1)
        cn:execute('DROP TABLE test2;')
        t.assert_equals(box.space.test2, nil)
    end)
end

-- gh-2617 DDL row_count either 0 or 1.
g.test_2617_DDL_row_count_either_0_or_1 = function(cg)
    cg.server:exec(function()
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])
        -- Test CREATE [IF NOT EXISTS] TABLE.
        cn:execute('CREATE TABLE test3(id INT PRIMARY KEY, a INT, b INT);')
        -- Rowcount = 1, although two tuples were created:
        -- for _space and for _index.
        cn:execute('INSERT INTO test3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);')
        cn:execute('CREATE TABLE IF NOT EXISTS test3(id INT PRIMARY KEY);')

        -- Test CREATE VIEW [IF NOT EXISTS] and
        --      DROP   VIEW [IF EXISTS].
        cn:execute('CREATE VIEW test3_view(id) AS SELECT id FROM test3;')
        local sql = [[CREATE VIEW IF NOT EXISTS test3_view(id)
                AS SELECT id FROM test3;]]
        cn:execute(sql)
        cn:execute('DROP VIEW test3_view;')
        cn:execute('DROP VIEW if EXISTS test3_view;')

        -- Test CREATE INDEX [IF NOT EXISTS] and
        --      DROP   INDEX [IF EXISTS].
        cn:execute('CREATE INDEX test3_sec ON test3(a, b);')
        cn:execute('CREATE INDEX IF NOT EXISTS test3_sec ON test3(a, b);')
        cn:execute('DROP INDEX test3_sec ON test3;')
        cn:execute('DROP INDEX IF EXISTS test3_sec ON test3;')

        -- Test CREATE TRIGGER [IF NOT EXISTS] and
        --      DROP   TRIGGER [IF EXISTS].
        sql = [[CREATE TRIGGER trig INSERT ON test3
                FOR EACH ROW BEGIN SELECT * FROM test3; END;]]
        cn:execute(sql)
        sql = [[CREATE TRIGGER IF NOT EXISTS trig INSERT ON
                test3 FOR EACH ROW BEGIN SELECT * FROM test3; END;]]
        cn:execute(sql)
        cn:execute('DROP TRIGGER trig;')
        cn:execute('DROP TRIGGER IF EXISTS trig;')

        -- Test DROP TABLE [IF EXISTS].
        -- Create more indexes, triggers and _truncate tuple.
        cn:execute('CREATE INDEX idx1 ON test3(a);')
        cn:execute('CREATE INDEX idx2 ON test3(b);')
        box.space.test3:truncate()
        sql = [[CREATE TRIGGER trig INSERT ON test3
                FOR EACH ROW BEGIN SELECT * FROM test3; END;]]
        cn:execute(sql)
        cn:execute('INSERT INTO test3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);')
        cn:execute('DROP TABLE test3;')
        cn:execute('DROP TABLE IF EXISTS test3;')
    end)
end

--
-- gh-2948: sql: remove unnecessary templates for binding
-- parameters.
g.test_2948_remove_unnecessary_temlates = function(cg)
    cg.server:exec(function()
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        local exp_err = "Syntax error at line 1 near '1'"
        local sql = 'SELECT ?1, ?2, ?3;'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {1, 2, 3})
                                          end)

        exp_err = 'Index of binding slots must start from 1'
        sql = 'SELECT $name, $name2;'
        t.assert_error_msg_content_equals(exp_err,
                                          function()
                                            cn:execute(sql, {1, 2})
                                          end)

        local parameters = {}
        parameters[1] = 11
        parameters[2] = 22
        parameters[3] = 33

        local res = cn:execute('SELECT $2, $1, $3;', parameters)
        t.assert_equals(res.rows, {{22, 11, 33}})

        res = cn:execute('SELECT * FROM test WHERE id = :1;', {1})
        t.assert_equals(res.rows, {{1, 2, '3'}})
    end)
end

-- gh-2602 obuf_alloc breaks the tuple in different slabs
g.test_2602_obuf_alloc_breaks = function(cg)
    cg.server:exec(function()
        box.cfg{memtx_max_tuple_size=8 * 1024 * 1024,
                vinyl_max_tuple_size=8 * 1024 * 1024}
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        cn:execute([[SET SESSION "sql_seq_scan" = true;]])
        local space = box.space.test
        space:replace{1, 1, string.rep('a', 4 * 1024 * 1024)}

        local exp = {
            {
                name = 'id',
                type = 'integer',
            },
            {
                name = 'a',
                type = 'number',
            },
            {
                name = 'b',
                type = 'string',
            },
        }
        local res = cn:execute('SELECT * FROM test;')
        t.assert_equals(res.metadata, exp)
        box.execute('DROP TABLE test;')
        cn:close()
    end)
end

--
-- gh-3107: async netbox.
--
g.test_3107_async_netbox = function(cg)
    cg.server:exec(function()
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user='test_user', password='test'})
        local sql = [[CREATE TABLE test
                      (id INTEGER PRIMARY KEY, a INTEGER, b INTEGER);]]
        cn:execute(sql)
        sql = 'INSERT INTO test VALUES (1, 1, 1);'
        local future1 = cn:execute(sql, nil, nil, {is_async = true})
        sql = 'INSERT INTO test VALUES (1, 2, 2);'
        local future2 = cn:execute(sql, nil, nil, {is_async = true})
        sql = 'INSERT INTO test VALUES (2, 2, 2), (3, 3, 3);'
        local future3 = cn:execute(sql, nil, nil, {is_async = true})
        future1:wait_result()

        local exp_err = 'Duplicate key exists in unique index '..
                        '"pk_unnamed_test_1" in space "test" with old '..
                        'tuple - [1, 1, 1] and new tuple - [1, 2, 2]'
        local _, err = future2:wait_result()
        t.assert_equals(tostring(err), exp_err)

        future3:wait_result()

        local exp = {
            {1, 1, 1},
            {2, 2, 2},
            {3, 3, 3},
        }
        sql = 'SELECT * FROM SEQSCAN test;'
        local future4 = cn:execute(sql, nil, nil, {is_async = true})
        t.assert_equals(future4:wait_result().rows, exp)
        cn:close()
        box.execute('DROP TABLE test;')
    end)
end

-- gh-2618 Return generated columns after INSERT in IPROTO.
-- Return all ids generated in current INSERT statement.
g.test_2618_Return_generated_columns_after_INSERT_in_IPROTO = function(cg)
    cg.server:exec(function(engine)
        local sql = [[CREATE TABLE test
                      (id INTEGER PRIMARY KEY AUTOINCREMENT, a INTEGER);]]
        local _, err = box.execute(sql)
        t.assert_equals(err, nil)
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen)
        cn:execute('INSERT INTO test VALUES (1, 1);')
        cn:execute('INSERT INTO test VALUES (null, 2);')
        cn:execute('UPDATE test SET a = 11 WHERE id == 1;')
        sql = [[INSERT INTO test VALUES (100, 1),
                (null, 1), (120, 1), (null, 1);]]
        cn:execute(sql)
        sql = [[INSERT INTO test VALUES (null, 1), (null, 1),
                (null, 1), (null, 1), (null, 1);]]
        cn:execute(sql)

        local exp = {
            {1, 11},
            {2, 2},
            {100, 1},
            {101, 1},
            {120, 1},
            {121, 1},
            {122, 1},
            {123, 1},
            {124, 1},
            {125, 1},
            {126, 1},
        }
        local res = cn:execute('SELECT * FROM SEQSCAN test;')
        t.assert_equals(res.rows, exp)

        local s = box.schema.create_space('test2', {engine = engine})
        local sq = box.schema.sequence.create('test2')
        s:create_index('pk', {sequence = 'test2'})
        local function push_id() s:replace{box.NULL} s:replace{box.NULL} end
        box.space.test:on_replace(push_id)
        cn:execute('INSERT INTO test VALUES (null, 1);')

        box.execute('CREATE TABLE test3 (id INT PRIMARY KEY AUTOINCREMENT);')
        box.schema.sequence.alter('test3', {min=-10000, step=-10})
        cn:execute('INSERT INTO test3 VALUES (null), (null), (null), (null);')

        box.execute('DROP TABLE test;')
        s:drop()
        sq:drop()
        box.execute('DROP TABLE test3;')

        --
        -- Ensure that FK inside CREATE TABLE does not affect row_count.
        --
        cn:execute('CREATE TABLE test (id INTEGER PRIMARY KEY, a INTEGER);')
        sql = [[CREATE TABLE test2 (id INTEGER PRIMARY KEY,
                ref INTEGER REFERENCES test(id));]]
        cn:execute(sql)
        cn:execute('DROP TABLE test2;')

        --
        -- Ensure that REPLACE is accounted twice in row_count. As delete +
        -- insert.
        --
        cn:execute('INSERT INTO test VALUES(1, 1);')
        cn:execute('INSERT OR REPLACE INTO test VALUES(1, 2);')
        cn:execute('DROP TABLE test;')

        -- SELECT returns unpacked msgpack.
        local format = {
            {name = 'id', type = 'integer'},
            {name = 'x', type = 'any'}
        }
        s = box.schema.space.create('test', {format=format})
        s:create_index('i1', {parts = {1, 'int'}})
        s:insert({1, {1,2,3}})
        s:insert({2, {a = 3}})

        exp = {
            {1, {1, 2, 3}},
            {2, {a = 3}},
        }
        res = cn:execute('SELECT * FROM SEQSCAN "test";')
        t.assert_equals(res.rows, exp)
        s:drop()

        -- Too many autogenerated ids leads to SEGFAULT.
        cn = remote.connect(box.cfg.listen)
        box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY AUTOINCREMENT);')
        for _ = 0, 1000 do cn:execute("INSERT INTO t1 VALUES (null);") end
        cn:execute("INSERT INTO t1 SELECT NULL FROM SEQSCAN t1;")
        box.execute('DROP TABLE t1;')

        cn:close()
    end, {cg.params.engine})
end

-- gh-3832: Some statements do not return column type
g.test_3832_some_statements_not_return_column_type = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
        local remote = require('net.box')
        local cn = remote.connect(box.cfg.listen,
                                  {user="test_user", password="test"})

        -- PRAGMA:
        local exp ={
            {
                name = 'cid',
                type = 'integer',
            },
            {
                name = 'name',
                type = 'text',
            },
            {
                name = 'type',
                type = 'text',
            },
            {
                name = 'notnull',
                type = 'integer',
            },
            {
                name = 'dflt_value',
                type = 'text'
            },
            {
                name = 'pk',
                type = 'integer'
            },
        }
        local res = cn:execute("PRAGMA table_info(t1);")
        t.assert_equals(res.metadata, exp)

        -- EXPLAIN
        exp = {
            {
                name = 'addr',
                type = 'integer'
            },
            {
                name = 'opcode',
                type = 'text',
            },
            {
                name = 'p1',
                type = 'integer',
            },
            {
                name = 'p2',
                type = 'integer',
            },
            {
                name = 'p3',
                type = 'integer',
            },
            {
                name = 'p4',
                type = 'text',
            },
            {
                name = 'p5',
                type = 'text',
            },
            {
                name = 'comment',
                type = 'text',
            },
        }
        res = cn:execute("EXPLAIN SELECT 1;")
        t.assert_equals(res.metadata, exp)

        exp = {
            {
                name = 'selectid',
                type = 'integer',
            },
            {
                name = 'order',
                type = 'integer',
            },
            {
                name = 'from',
                type = 'integer',
            },
            {
                name = 'detail',
                type = 'text',
            },
        }
        res = cn:execute("EXPLAIN QUERY PLAN SELECT COUNT(*) FROM t1;")
        t.assert_equals(res.metadata, exp)

        -- Make sure that built-in functions have a right returning type.
        --
        res = cn:execute("SELECT ZEROBLOB(1);")
        t.assert_equals(res.rows, {{string.char(0)}})
        -- randomblob() returns different results each time, so check only
        -- type in meta.
        --
        exp = {
            {
                name = 'COLUMN_1',
                type = 'varbinary',
            },
        }
        res = cn:execute("SELECT RANDOMBLOB(1);")
        t.assert_equals(res.metadata, exp)
        -- Type set during compilation stage, and since min/max are accept
        -- arguments of all scalar type, we can't say nothing more than
        -- SCALAR.
        --
        res = cn:execute("SELECT LEAST(1, 2, 3);")
        t.assert_equals(res.rows, {{1}})

        res = cn:execute("SELECT GREATEST(1, 2, 3);")
        t.assert_equals(res.rows, {{3}})

        cn:close()
        box.execute('DROP TABLE t1')

        box.schema.user.revoke('test_user', 'read,write,execute', 'universe')
        box.schema.user.revoke('test_user', 'create', 'space')
        --space = nil
    end)
end

--
-- gh-4756: PREPARE and EXECUTE statistics should be present in box.stat()
--
g.test_4756_PREPARE_and_EXECUTE_statistics = function(cg)
    cg.server:exec(function()
        local p = box.stat().PREPARE.total
        local e = box.stat().EXECUTE.total

        local s = box.prepare([[ SELECT ?; ]])

        local res = s:execute({42})
        t.assert_equals(res.rows, {{42}})

        res = box.execute('SELECT 1;')
        t.assert_equals(res.rows, {{1}})

        box.unprepare(s)

        t.assert_equals(box.stat().PREPARE.total, p + 1)
        t.assert_equals(box.stat().EXECUTE.total, e + 2)

        -- Cleanup xlog
        t.assert_equals(box.snapshot(), 'ok')
        box.schema.user.drop('test_user')
    end)
end
