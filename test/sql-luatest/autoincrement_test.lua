local server = require('luatest.server')
local t = require('luatest')

local g = t.group("autoincrement", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
-- gh-2981: Make sure that values ​​inserted
-- using AUTOINCREMENT do not bypass CHECK.
--
g.test_2981_check_autoinc = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1 (s1 INTEGER PRIMARY KEY AUTOINCREMENT,
                                       s2 INTEGER, CHECK (s1 <> 19));]])
        box.execute([[CREATE TABLE t2 (s1 INTEGER PRIMARY KEY AUTOINCREMENT,
                      s2 INTEGER, CHECK (s1 <> 19 AND s1 <> 25));]])
        box.execute([[CREATE TABLE t3 (s1 INTEGER PRIMARY KEY AUTOINCREMENT,
                                       s2 INTEGER, CHECK (s1 < 10));]])

        box.execute("INSERT INTO t1 VALUES (18, null);")
        local _, err = box.execute("INSERT INTO t1(s2) VALUES (null);")
        local err_exp = "Check constraint 'ck_unnamed_t1_1' failed for a tuple"
        t.assert_equals(err.message, err_exp)

        box.execute("INSERT INTO t2 VALUES (18, null);")
        _, err = box.execute("INSERT INTO t2(s2) VALUES (null);")
        err_exp = "Check constraint 'ck_unnamed_t2_1' failed for a tuple"
        t.assert_equals(err.message, err_exp)

        box.execute("INSERT INTO t2 VALUES (24, null);")
        _, err = box.execute("INSERT INTO t2(s2) VALUES (null);")
        t.assert_equals(err.message, err_exp)

        box.execute("INSERT INTO t3 VALUES (9, null)")
        _, err = box.execute("INSERT INTO t3(s2) VALUES (null)")
        err_exp = "Check constraint 'ck_unnamed_t3_1' failed for a tuple"
        t.assert_equals(err.message, err_exp)

        box.execute("DROP TABLE t1")
        box.execute("DROP TABLE t2")
        box.execute("DROP TABLE t3")

        box.func.check_t1_ck_unnamed_t1_1:drop()
        box.func.check_t2_ck_unnamed_t2_1:drop()
        box.func.check_t3_ck_unnamed_t3_1:drop()
    end)
end
