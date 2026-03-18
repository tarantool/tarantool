local server = require('luatest.server')
local t = require('luatest')

local g = t.group("transition", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s';]]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_transition = function(cg)
    cg.server:exec(function()
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

        -- Cleanup.
        box.execute("DROP INDEX barfoo2 ON barfoo;")
        box.execute("DROP TABLE foobar;")
        box.execute("DROP TABLE barfoo;")

        -- Attempt to create a table lacking PRIMARY KEY.
        box.execute("CREATE TABLE without_rowid_lacking_primary_key(x SCALAR);")

        -- Create a table with implicit indices (used to SEGFAULT).
        box.execute("CREATE TABLE implicit_indices(a INT PRIMARY KEY, " ..
                    "b INT, c INT, d TEXT UNIQUE);")
        box.execute("DROP TABLE implicit_indices;")
    end)
end
