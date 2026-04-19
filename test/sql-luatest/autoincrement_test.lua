local server = require('luatest.server')
local t = require('luatest')

local g = t.group("autoincrement", {{engine = 'memtx'}, {engine = 'vinyl'}})

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

g.test_drop_only_generated_sequences = function(cg)
    cg.server:exec(function(engine)
        -- Not automatic sequence should not be removed by DROP TABLE.
        t.assert_equals(#box.space._sequence:select(), 0)
        local test_space = box.schema.create_space('t', {
            engine = engine,
            format = {{'i', 'integer'}},
        })
        local seq = box.schema.sequence.create('S')
        t.assert_equals(#box.space._sequence:select(), 1)
        test_space:create_index('I', {sequence = 'S'})
        box.execute('DROP TABLE t;')
        t.assert_equals(#box.space._sequence:select(), 1)
        seq:drop()
        t.assert_equals(#box.space._sequence:select(), 0)

        -- Automatic sequence should be removed at DROP TABLE, together
        -- with all the grants.
        box.execute('CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT);')
        t.assert_equals(#box.space._sequence:select(), 1)
        box.execute('DROP TABLE t;')
        t.assert_equals(#box.space._sequence:select(), 0)
    end, {cg.params.engine})
end

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

        box.execute("INSERT INTO t3 VALUES (9, null);")
        _, err = box.execute("INSERT INTO t3(s2) VALUES (null);")
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

--
-- Make sure that when inserting, values are inserted in the given
-- order when ephemeral space is used.
--
g.test_4256_do_not_change_order_during_insertion = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY AUTOINCREMENT);]])

        -- In order for this INSERT to use the ephemeral space, we created
        -- this trigger.
        box.execute([[CREATE TRIGGER r AFTER INSERT ON t
                      FOR EACH ROW BEGIN SELECT 1; END;]])
        local res = box.execute([[INSERT INTO t VALUES (1), (NULL), (10),
                                  (NULL), (NULL), (3), (NULL);]])
        local exp = {
            autoincrement_ids = {2, 11, 12, 13},
            row_count = 7,
        }
        t.assert_equals(res, exp)
        res = box.execute([[SELECT * FROM t;]])
        t.assert_equals(res.rows, {{1}, {2}, {3}, {10}, {11}, {12}, {13}})

        box.execute([[DROP TABLE t;]])
    end)
end
