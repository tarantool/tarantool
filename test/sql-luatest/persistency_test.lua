local server = require('luatest.server')
local t = require('luatest')

local g = t.group("transition", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- This test ensures that there is no "access denied" error after a restart.
--
g.test_2483_remote_persistency_check = function(cg)
    cg.server:exec(function()
        box.schema.user.create('new_user', {password = 'test'})
        box.schema.user.grant('new_user',
                              'read,write,create,execute', 'universe')
        -- Create a table and insert a datum.
        box.execute([[CREATE TABLE t(id int PRIMARY KEY);]])
        box.execute([[INSERT INTO t (id) VALUES (1);]])

        -- Sanity check.
        local res = box.execute([[SELECT * FROM SEQSCAN t;]])
        t.assert_equals(res.rows, {{1}})
    end)

    cg.server:restart()
    cg.server:exec(function()
        -- Connect to ourself.
        local c = require('net.box').connect(box.cfg.listen, {
                                                user = 'new_user',
                                                password = 'test',
                                             })

        local cmd = [[return box.execute('SELECT * FROM SEQSCAN t;')]]
        local res, err = c:eval(cmd)
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, {{1}})

        box.execute([[DROP TABLE t;]])
        box.schema.user.drop('new_user')
    end)
end

--
-- This test checks whether the unique index is restored correctly after
-- a restart.
--
g.test_2808_inline_unique_presistency_check = function(cg)
    cg.server:exec(function()
        -- Create a table and insert a datum
        box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT, UNIQUE(b));]])
        box.execute([[INSERT INTO t1 VALUES(1, 2);]])

        -- Sanity check.
        local exp = {
            metadata = {
                {
                    name = "a",
                    type = "integer",
                },
                {
                    name = "b",
                    type = "integer",
                },
            },
            rows = {{1, 2}},
        }
        local res = box.execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res, exp)
    end)

    cg.server:restart()
    cg.server:exec(function()
        box.execute([[INSERT INTO t1 VALUES(2, 3);]])

        -- Sanity check.
        local exp = {
            metadata = {
                {
                    name = "a",
                    type = "integer",
                },
                {
                    name = "b",
                    type = "integer",
                },
            },
            rows = {{1, 2}, {2, 3}},
        }
        local res = box.execute([[SELECT * FROM SEQSCAN t1;]])
        t.assert_equals(res, exp)

        -- Cleanup.
        box.execute([[DROP TABLE t1;]])
        t.assert_equals(box.space.t1, nil)
    end)
end

g.test_transition = function(cg)
    cg.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])

        -- Create space.
        box.execute("CREATE TABLE foobar (foo INT PRIMARY KEY, bar TEXT);")

        -- Prepare data.
        box.execute("INSERT INTO foobar VALUES (1, 'foo');")
        box.execute("INSERT INTO foobar VALUES (2, 'bar');")
        box.execute("INSERT INTO foobar VALUES (1000, 'foobar');")

        local sql = "INSERT INTO foobar VALUES (1, 'duplicate');"
        local _, err = box.execute(sql)
        local exp_err = 'Duplicate key exists in unique index ' ..
                        '"pk_unnamed_foobar_1" in space "foobar" with old ' ..
                        'tuple - [1, "foo"] and new tuple - [1, "duplicate"]'
        t.assert_equals(err.message, exp_err)

        -- Simple select.
        local res = box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar;")
        local exp = {
            {'foo', 1, 42, 'awesome'},
            {'bar', 2, 42, 'awesome'},
            {'foobar', 1000, 42, 'awesome'},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT bar, foo, 42, 'awesome' FROM foobar LIMIT 2;")
        exp = {
            {'foo', 1, 42, 'awesome'},
            {'bar', 2, 42, 'awesome'},
        }
        t.assert_equals(res.rows, exp)
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo = 2;"
        res = box.execute(sql)
        t.assert_equals(res.rows, {{'bar', 2, 42, 'awesome'}})
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo > 2;"
        res = box.execute(sql)
        t.assert_equals(res.rows, {{'foobar', 1000, 42, 'awesome'}})
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo >= 2;"
        res = box.execute(sql)
        exp = {
            {'bar', 2, 42, 'awesome'},
            {'foobar', 1000, 42, 'awesome'},
        }
        t.assert_equals(res.rows, exp)
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo = 10000;"
        res = box.execute(sql)
        t.assert_equals(res.rows, {})
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo > 10000;"
        res = box.execute(sql)
        t.assert_equals(res.rows, {})
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo < 2;"
        res = box.execute(sql)
        t.assert_equals(res.rows, {{'foo', 1, 42, 'awesome'}})
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo < 2.001;"
        res = box.execute(sql)
        exp = {
            {'foo', 1, 42, 'awesome'},
            {'bar', 2, 42, 'awesome'},
        }
        t.assert_equals(res.rows, exp)
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo <= 2;"
        res = box.execute(sql)
        t.assert_equals(res.rows, exp)
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo < 100;"
        res = box.execute(sql)
        t.assert_equals(res.rows, exp)
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE bar = 'foo';"
        res = box.execute(sql)
        t.assert_equals(res.rows, {{'foo', 1, 42, 'awesome'}})
        res = box.execute("SELECT COUNT(*) FROM foobar;")
        t.assert_equals(res.rows, {{3}})
        res = box.execute("SELECT COUNT(*) FROM foobar WHERE bar='foo';")
        t.assert_equals(res.rows, {{1}})
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar ORDER BY bar;"
        res = box.execute(sql)
        exp = {
            {'bar', 2, 42, 'awesome'},
            {'foo', 1, 42, 'awesome'},
            {'foobar', 1000, 42, 'awesome'},
        }
        t.assert_equals(res.rows, exp)
        sql = "SELECT bar, foo, 42, 'awesome' FROM foobar ORDER BY bar DESC;"
        res = box.execute(sql)
        exp = {
            {'foobar', 1000, 42, 'awesome'},
            {'foo', 1, 42, 'awesome'},
            {'bar', 2, 42, 'awesome'},
        }
        t.assert_equals(res.rows, exp)

        -- Updates.
        res = box.execute("REPLACE INTO foobar VALUES (1, 'cacodaemon');")
        t.assert_equals(res, {row_count = 2})
        res = box.execute("SELECT COUNT(*) FROM foobar WHERE foo = 1;")
        t.assert_equals(res.rows, {{1}})
        sql = "SELECT COUNT(*) FROM foobar WHERE bar = 'cacodaemon';"
        res = box.execute(sql)
        t.assert_equals(res.rows, {{1}})
        res = box.execute("DELETE FROM foobar WHERE bar = 'cacodaemon';")
        t.assert_equals(res, {row_count = 1})
        sql = "SELECT COUNT(*) FROM foobar WHERE bar = 'cacodaemon';"
        res = box.execute(sql)
        t.assert_equals(res.rows, {{0}})

        -- Multi-index.

        -- Create space.
        box.execute("CREATE TABLE barfoo (bar TEXT, foo NUMBER PRIMARY KEY);")
        box.execute("CREATE UNIQUE INDEX barfoo2 ON barfoo(bar);")

        -- Prepare data.
        box.execute("INSERT INTO barfoo VALUES ('foo', 1);")
        box.execute("INSERT INTO barfoo VALUES ('bar', 2);")
        box.execute("INSERT INTO barfoo VALUES ('foobar', 1000);")

        -- Prove barfoo2 was created.
        _, err = box.execute("INSERT INTO barfoo VALUES ('xfoo', 1);")
        exp_err = 'Duplicate key exists in unique index ' ..
                  '"pk_unnamed_barfoo_1" in space "barfoo" with old ' ..
                  'tuple - ["foo", 1] and new tuple - ["xfoo", 1]'
        t.assert_equals(err.message, exp_err)

        res = box.execute("SELECT foo, bar FROM barfoo;")
        exp = {
            {1, 'foo'},
            {2, 'bar'},
            {1000, 'foobar'},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT foo, bar FROM barfoo WHERE foo == 2;")
        t.assert_equals(res.rows, {{2, 'bar'}})
        res = box.execute("SELECT foo, bar FROM barfoo WHERE bar == 'foobar';")
        t.assert_equals(res.rows, {{1000, 'foobar'}})
        res = box.execute("SELECT foo, bar FROM barfoo WHERE foo >= 2;")
        exp = {
            {2, 'bar'},
            {1000, 'foobar'},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT foo, bar FROM barfoo WHERE foo <= 2;")
        exp = {
            {1, 'foo'},
            {2, 'bar'},
        }
        t.assert_equals(res.rows, exp)

        -- Attempt to create a table lacking PRIMARY KEY.
        sql = "CREATE TABLE without_rowid_lacking_primary_key(x SCALAR);"
        _, err = box.execute(sql)
        exp_err = "Failed to create space " ..
                  "'without_rowid_lacking_primary_key': PRIMARY KEY missing"
        t.assert_equals(err.message, exp_err)

        -- Create a table with implicit indices (used to SEGFAULT).
        res = box.execute([[CREATE TABLE implicit_indices(a INT PRIMARY KEY,
                            b INT, c INT, d TEXT UNIQUE);]])
        t.assert_equals(res, {row_count = 1})
        box.execute("DROP TABLE implicit_indices;")

        -- Create a trigger.
        sql = "CREATE TRIGGER tfoobar AFTER INSERT ON foobar FOR EACH ROW " ..
              "BEGIN INSERT INTO barfoo VALUES ('trigger test', 9999); END;"
        box.execute(sql)
        res = box.execute([[SELECT "name", "opts" FROM "_trigger";]])
        t.assert_equals(res.rows, {{'tfoobar', {sql = sql}}})

        -- Many entries.
        box.execute("CREATE TABLE t1(a INT, b INT, c INT, PRIMARY KEY(b, c));")
        sql = "WITH RECURSIVE cnt(x) AS (VALUES(1) UNION ALL SELECT x + 1 " ..
              "FROM cnt WHERE x < 1000) INSERT INTO t1 " ..
              "SELECT x, x%40, x/40 FROM cnt;"
        res = box.execute(sql)
        t.assert_equals(res, {row_count = 1000})
        res = box.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;")
        exp = {
            {840},
            {880},
            {920},
            {960},
            {1000},
            {1},
            {41},
            {81},
            {121},
            {161},
        }
        t.assert_equals(res.rows, exp)
    end)
    cg.server:restart()
    cg.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])

        -- Prove that trigger survived.
        local res = box.execute([[SELECT "name", "opts" FROM "_trigger";]])
        local sql = "CREATE TRIGGER tfoobar AFTER INSERT ON foobar FOR EACH " ..
                    "ROW BEGIN INSERT INTO barfoo VALUES " ..
                    "('trigger test', 9999); END;"
        t.assert_equals(res.rows, {{'tfoobar', {sql = sql}}})

        -- Prove that trigger functional.
        sql = "INSERT INTO foobar VALUES ('foobar trigger test', 8888);"
        local _, err = box.execute(sql)
        local exp_err = "Type mismatch: can not convert " ..
                        "string('foobar trigger test') to integer"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT * FROM barfoo WHERE foo = 9999;")
        t.assert_equals(res.rows, {})

        -- Prove that trigger can be dropped just once.
        res = box.execute("DROP TRIGGER tfoobar;")
        t.assert_equals(res, {row_count = 1})
        -- Should error.
        _, err = box.execute("DROP TRIGGER tfoobar;")
        t.assert_equals(err.message, "Trigger 'tfoobar' doesn't exist")
        -- Should be empty.
        res = box.execute([[SELECT "name", "opts" FROM "_trigger";]])
        t.assert_equals(res.rows, {})

        res = box.execute("SELECT * FROM barfoo;")
        local exp = {
            {'foo', 1},
            {'bar', 2},
            {'foobar', 1000},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT * FROM foobar;")
        exp = {
            {2, 'bar'},
            {1000, 'foobar'},
        }
        t.assert_equals(res.rows, exp)
        res = box.execute("SELECT a FROM t1 ORDER BY b, a LIMIT 10 OFFSET 20;")
        exp = {
            {840},
            {880},
            {920},
            {960},
            {1000},
            {1},
            {41},
            {81},
            {121},
            {161},
        }
        t.assert_equals(res.rows, exp)

        -- Cleanup.
        box.execute("DROP INDEX barfoo2 ON barfoo;")
        box.execute("DROP TABLE foobar;")
        box.execute("DROP TABLE barfoo;")
        box.execute("DROP TABLE t1;")
    end)
end
