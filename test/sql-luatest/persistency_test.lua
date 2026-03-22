local server = require('luatest.server')
local t = require('luatest')

local g = t.group("persistency", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)


g.test_persistency = function(cg)
    cg.server:exec(function()
        -- Create space.
        box.execute("CREATE TABLE foobar (foo INT PRIMARY KEY, bar TEXT);")

        -- Prepare data.
        box.execute("INSERT INTO foobar VALUES (1, 'foo');")
        box.execute("INSERT INTO foobar VALUES (2, 'bar');")
        box.execute("INSERT INTO foobar VALUES (1000, 'foobar');")

        box.execute("INSERT INTO foobar VALUES (1, 'duplicate');")

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
        local sql = "SELECT bar, foo, 42, 'awesome' FROM foobar WHERE foo = 2;"
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
        res = box.execute("SELECT COUNT(*) FROM foobar WHERE bar = 'foo';")
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
        res = box.execute([[SELECT COUNT(*) FROM foobar
                            WHERE bar = 'cacodaemon';]])
        t.assert_equals(res.rows, {{0}})

        -- Multi-index.

        -- Create space.
        box.execute("CREATE TABLE barfoo (bar TEXT, foo NUMBER PRIMARY KEY);")
        box.execute("CREATE UNIQUE INDEX barfoo2 ON barfoo(bar);")

        -- Prepare data.
        box.execute("INSERT INTO barfoo VALUES ('foo', 1);")
        box.execute("INSERT INTO barfoo VALUES ('bar', 2);")
        box.execute("INSERT INTO barfoo VALUES ('foobar', 1000);")

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

        -- Prove that trigger still persistent.
        res = box.execute([[SELECT "name", "opts" FROM "_trigger";]])
        sql = "CREATE TRIGGER tfoobar AFTER INSERT ON foobar FOR EACH ROW " ..
              "BEGIN INSERT INTO barfoo VALUES ('trigger test', 9999); END;"
        t.assert_equals(res.rows, {{'tfoobar', {sql = sql}}})

        -- Prove that trigger can be dropped just once.
        res = box.execute("DROP TRIGGER tfoobar;")
        t.assert_equals(res, {row_count = 1})
        -- Should error.
        _, err = box.execute("DROP TRIGGER tfoobar;")
        t.assert_equals(err.message, "Trigger 'tfoobar' doesn't exist")
        -- Should be empty.
        res = box.execute([[SELECT "name", "opts" FROM "_trigger";]])
        t.assert_equals(res.rows, {})

        -- Prove barfoo2 still exists.
        _, err = box.execute("INSERT INTO barfoo VALUES ('xfoo', 1);")
        exp_err = 'Duplicate key exists in unique index ' ..
                  '"pk_unnamed_barfoo_1" in space "barfoo" with old tuple - ' ..
                  '["foo", 1] and new tuple - ["xfoo", 1]'
        t.assert_equals(err.message, exp_err)

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
        box.execute("DROP TABLE foobar;")
        box.execute("DROP TABLE barfoo;")
        box.execute("DROP TABLE t1;")
    end)
end
