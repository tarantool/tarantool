local server = require('luatest.server')
local t = require('luatest')

local g = t.group("row_count", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

--
-- Test cases concerning row count calculations.
--
g.test_row_count = function(cg)
    cg.server:exec(function()
        local res = box.execute("CREATE TABLE t1 (s1 VARCHAR(10) PRIMARY KEY);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        res = box.execute([[CREATE TABLE t2 (s1 VARCHAR(10) PRIMARY KEY,
                                             s2 VARCHAR(10));]])
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute([[CREATE TABLE t3 (i1 INT UNIQUE, i2 INT,
                                             i3 INT PRIMARY KEY);]])
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO t3 VALUES (0, 0, 0);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute([[CREATE TRIGGER x AFTER DELETE ON t1 FOR EACH ROW
                            BEGIN UPDATE t3 SET i1 = i1 + ROW_COUNT(); END;]])
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("INSERT INTO t1 VALUES ('a');")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("INSERT INTO t2 VALUES ('a', 'a');")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("INSERT INTO t1 VALUES ('b'), ('c'), ('d');")
        t.assert_equals(res, {row_count = 3})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{3}})
        -- REPLACE is accounted for two operations: DELETE + INSERT.
        res = box.execute("REPLACE INTO t2 VALUES('a', 'c');")
        t.assert_equals(res, {row_count = 2})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{2}})
        res = box.execute("DELETE FROM t1;")
        t.assert_equals(res, {row_count = 4})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{4}})
        local sql = "INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);"
        res = box.execute(sql)
        t.assert_equals(res, {row_count = 3})
        res = box.execute("TRUNCATE TABLE t3;")
        t.assert_equals(res, {row_count = 0})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        sql = "INSERT INTO t3 VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);"
        res = box.execute(sql)
        t.assert_equals(res, {row_count = 3})
        res = box.execute("UPDATE t3 SET i2 = 666;")
        t.assert_equals(res, {row_count = 3})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{3}})

        -- All statements that do not add, remove, or modify inserted
        -- tuples must return 0 (zero).
        res = box.execute("START TRANSACTION;")
        t.assert_equals(res, {row_count = 0})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        res = box.execute("COMMIT;")
        t.assert_equals(res, {row_count = 0})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        local _, err = box.execute("COMMIT;")
        local exp_err = "Failed to execute SQL statement: cannot commit - " ..
                        "no transaction is active"
        t.assert_equals(err.message, exp_err)
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        sql = "EXPLAIN QUERY PLAN INSERT INTO t1 VALUES ('b'), ('c'), ('d');"
        res = box.execute(sql)
        t.assert_equals(res.rows, {})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{0}})
        res = box.space._session_settings:get('sql_recursive_triggers')
        t.assert_equals(res, {'sql_recursive_triggers', true})

        -- Clean-up.
        box.execute("DROP TABLE t2;")
        box.execute("DROP TABLE t3;")
        box.execute("DROP TABLE t1;")
    end)
end

--
-- gh-3816: DELETE optimization returns valid number of
-- deleted tuples.
--
g.test_3816_number_of_deleted_tuples = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (i1 INT UNIQUE, i2 INT,
                                      i3 INT PRIMARY KEY);]])
        box.execute("INSERT INTO t VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);")
        local res = box.execute("DELETE FROM t WHERE 0 = 0;")
        t.assert_equals(res, {row_count = 3})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{3}})
        local sql = "INSERT INTO t VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3);"
        res = box.execute(sql)
        t.assert_equals(res, {row_count = 3})
        res = box.execute("DELETE FROM t;")
        t.assert_equals(res, {row_count = 3})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{3}})
        box.execute("DROP TABLE t;")
        -- But triggers still shouldn't be accounted.
        res = box.execute("CREATE TABLE tt1 (id INT PRIMARY KEY);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("CREATE TABLE tt2 (id INT PRIMARY KEY);")
        t.assert_equals(res, {row_count = 1})
        res = box.execute([[CREATE TRIGGER tr1 AFTER DELETE ON tt1
                            FOR EACH ROW BEGIN DELETE FROM tt2; END;]])
        t.assert_equals(res, {row_count = 1})
        res = box.execute("INSERT INTO tt1 VALUES (1), (2), (3);")
        t.assert_equals(res, {row_count = 3})
        res = box.execute("INSERT INTO tt2 VALUES (1), (2), (3);")
        t.assert_equals(res, {row_count = 3})
        res = box.execute("DELETE FROM tt1 WHERE id = 2;")
        t.assert_equals(res, {row_count = 1})
        res = box.execute("SELECT ROW_COUNT();")
        t.assert_equals(res.rows, {{1}})
        res = box.execute("SELECT * FROM tt2;")
        t.assert_equals(res.rows, {})
        box.execute("DROP TABLE tt1;")
        box.execute("DROP TABLE tt2;")
    end)
end

--
-- gh-4188: make sure that in case of INSERT OR IGNORE only
-- successful inserts are counted.
--
g.test_4188_insert_or_ignore = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT,
                                      a INT CHECK (a > 0));]])
        local res = box.execute([[INSERT OR IGNORE INTO t VALUES
                                  (null, 1), (null, -1), (null, 2);]])
        local exp = {
            autoincrement_ids = {1, 3},
            row_count = 2,
        }
        t.assert_equals(res, exp)
        res = box.execute("SELECT * FROM t;")
        t.assert_equals(res.rows, {{1, 1}, {3, 2}})
        box.execute("DROP TABLE t;")
        box.func.check_t_ck_unnamed_t_a_1:drop()
    end)
end

--
-- gh-4363: make sure that row_count has increased in the case of
-- ALTER TABLE <table> ADD CONSTRAINT <constraint> CHECK(<expr>);.
--
g.test_4363_alter = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t1(id INTEGER PRIMARY KEY);')
        local sql = 'ALTER TABLE t1 ADD CONSTRAINT ck1 CHECK (id > 0);'
        local res = box.execute(sql)
        t.assert_equals(res, {row_count = 1})
        box.execute('DROP TABLE t1;')
        box.func.check_t1_ck1:drop()
    end)
end
