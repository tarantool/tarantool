local server = require('luatest.server')
local t = require('luatest')

local g = t.group("update", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

--
-- gh-2251: Make sure that the UPDATE operation affects all eligible rows.
--
g.test_2251_multiple_update = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1(a INTEGER PRIMARY KEY,
                      b INT UNIQUE, e INT);]])
        box.execute("INSERT INTO t1 VALUES(1, 4, 6);")
        box.execute("INSERT INTO t1 VALUES(2, 5, 7);")

        local res = box.execute([[UPDATE t1 SET e = e + 1
                                  WHERE b IN (SELECT b FROM t1);]])
        t.assert_equals(res, {row_count = 2})

        res = box.execute("SELECT e FROM t1")
        t.assert_equals(res.rows, {{7}, {8}})

        box.execute([[CREATE TABLE t2(a INTEGER PRIMARY KEY, b INT UNIQUE,
                      c NUMBER, d NUMBER, e INT, UNIQUE(c, d));]])
        box.execute("INSERT INTO t2 VALUES(1, 2, 3, 4, 5);")
        box.execute("INSERT INTO t2 VALUES(2, 3, 4, 4, 6);")

        res = box.execute([[UPDATE t2 SET e = e + 1 WHERE b
                            IN (SELECT b FROM t2);]])
        t.assert_equals(res, {row_count = 2})

        res = box.execute("SELECT e FROM t2")
        t.assert_equals(res.rows, {{6}, {7}})

        box.execute("DROP TABLE t1")
        box.execute("DROP TABLE t2")
    end)
end

-- gh-3566: UPDATE OR IGNORE causes deletion of old entry.
g.test_3566_update_ignore_delection_old_entry = function(cg)
    cg.server:exec(function()
        local sql = "CREATE TABLE tj (s0 INT PRIMARY KEY, "..
                    "s1 INT UNIQUE, s2 INT);"
        box.execute(sql)
        box.execute("INSERT INTO tj VALUES (1, 1, 2), (2, 2, 3);")
        box.execute("CREATE UNIQUE INDEX i ON tj (s2);")
        box.execute("UPDATE OR IGNORE tj SET s1 = s1 + 1;")

        local exp = {
            {1, 2},
            {3, 3},
        }
        local res = box.execute("SELECT s1, s2 FROM tj;")
        t.assert_equals(res.rows, exp)

        box.execute("UPDATE OR IGNORE tj SET s2 = s2 + 1;")

        exp = {
            {1, 2},
            {3, 4},
        }
        res = box.execute("SELECT s1, s2 FROM tj;")
        t.assert_equals(res.rows, exp)
        box.execute("DROP TABLE tj;")
    end)
end

