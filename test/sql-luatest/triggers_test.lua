local server = require('luatest.server')
local t = require('luatest')

local g = t.group("triggers", {{engine = 'memtx'}, {engine = 'vinyl'}})

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
-- gh-3273: Move Triggers to server
--
 g.test_triggers = function(cg)
    cg.server:exec(function()
        -- Check that the names and queries of all registered triggers
        -- match expected.
        local function check_triggers_def(exp)
            local res = {}
            for _, trigger_def in box.space._trigger:pairs() do
                table.insert(res, {trigger_def.name, trigger_def.opts})
            end
            t.assert_equals(res, exp)
        end

        box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
        box.execute("CREATE TABLE t2(x INTEGER PRIMARY KEY);")

        local sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 "..
                    "FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"
        box.execute(sql)
        local exp = {{"t1t", {sql = sql}}}
        check_triggers_def(exp)

        local space_id = box.space.t1.id
        local _trigger = box.space._trigger

        -- Checks for LUA tuples.
        sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 "..
              "FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"
        local tuple = {"T1t", space_id, {sql = sql}}
        local exp_err = {
            details = "trigger name does not match extracted from SQL",
            message = "Failed to execute SQL statement: "..
                      "trigger name does not match extracted from SQL",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        sql = "CREATE TRIGGER t12t AFTER INSERT ON t1 "..
              "FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;"
        tuple = {"T1t", space_id, {sql = sql}}
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        sql = "CREATE TRIGGER t2t AFTER "..
              "INSERT ON t1 FOR EACH ROW BEGIN "..
              "INSERT INTO t2 VALUES(1); END;"
        tuple = {"t2t", space_id + 1, {sql = sql}}
        exp_err = {
            details = "trigger space_id does not match the value "..
                      "resolved on AST building from SQL",
            message = "Failed to execute SQL statement: "..
                      "trigger space_id does not match "..
                      "the value resolved on AST building from SQL",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)
        check_triggers_def(exp)

        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
        check_triggers_def({})

        box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
        sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN "..
              "INSERT INTO t2 VALUES(1); END;"
        box.execute(sql)
        check_triggers_def({{"t1t", {sql = sql}}})

        space_id = box.space.t1.id

        -- Test, didn't trigger t1t degrade.
        box.execute("INSERT INTO t1 VALUES(1);")
        exp = {
            metadata = {
                {
                    name = "x",
                    type = "integer",
                },
            },
            rows = {{1}},
        }
        t.assert_equals(box.execute("SELECT * FROM t2;"), exp)
        box.execute("DELETE FROM t2;")

        -- Test triggers.
        sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 FOR "..
              "EACH ROW BEGIN INSERT INTO t2 VALUES(2); END;"
        tuple = {"t2t", space_id, {sql = sql}}
        _trigger:insert(tuple)
        sql = "CREATE TRIGGER t3t AFTER INSERT ON t1 FOR "..
              "EACH ROW BEGIN INSERT INTO t2 VALUES(3); END;"
        tuple = {"t3t", space_id, {sql = sql}}
        _trigger:insert(tuple)
        exp = {
            {
                "t1t",
                {
                    sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 FOR "..
                          "EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;",
                },
            },
            {
                "t2t",
                {
                    sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 FOR "..
                          "EACH ROW BEGIN INSERT INTO t2 VALUES(2); END;",
                },
            },
            {
                "t3t",
                {
                     sql = "CREATE TRIGGER t3t AFTER INSERT ON t1 FOR "..
                           "EACH ROW BEGIN INSERT INTO t2 VALUES(3); END;",
                },
            },
        }
        check_triggers_def(exp)
        box.execute("INSERT INTO t1 VALUES(2);")

        exp = {
            metadata = {
                {
                    name = "x",
                    type = "integer",
                },
            },
            rows = {{1}, {2}, {3}},
        }
        t.assert_equals(box.execute("SELECT * FROM t2;"), exp)
        box.execute("DELETE FROM t2;")

        -- Test t1t after t2t and t3t drop.
        box.execute("DROP TRIGGER t2t;")
        _trigger:delete("t3t")
        sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN "..
              "INSERT INTO t2 VALUES(1); END;"
        check_triggers_def({{"t1t", {sql = sql}}})
        box.execute("INSERT INTO t1 VALUES(3);")

        exp = {
            metadata = {
                {
                    name = "x",
                    type = "integer",
                },
            },
            rows = {{1}},
        }
        t.assert_equals(box.execute("SELECT * FROM t2;"), exp)
        box.execute("DELETE FROM t2;")

        -- Insert new SQL t2t and t3t.
        sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 FOR EACH ROW BEGIN "..
              "INSERT INTO t2 VALUES(2); END;"
        box.execute(sql)
        sql = "CREATE TRIGGER t3t AFTER INSERT ON t1 FOR EACH ROW BEGIN "..
              "INSERT INTO t2 VALUES(3); END;"
        box.execute(sql)
        exp = {
            {
                "t1t",
                {
                    sql = "CREATE TRIGGER t1t AFTER INSERT ON t1 FOR "..
                          "EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;",
                },
            },
            {
                "t2t",
                {
                    sql = "CREATE TRIGGER t2t AFTER INSERT ON t1 FOR "..
                          "EACH ROW BEGIN INSERT INTO t2 VALUES(2); END;",
                },
            },
            {
                "t3t",
                {
                    sql = "CREATE TRIGGER t3t AFTER INSERT ON t1 FOR "..
                          "EACH ROW BEGIN INSERT INTO t2 VALUES(3); END;",
                },
            },
        }
        check_triggers_def(exp)
        box.execute("INSERT INTO t1 VALUES(4);")

        exp = {
            metadata = {
                {
                    name = "x",
                    type = "integer",
                },
            },
            rows = {{1}, {2}, {3}},
        }
        t.assert_equals(box.execute("SELECT * FROM t2;"), exp)

        -- Clean up.
        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
        box.execute("DROP TABLE t2;")
        t.assert_equals(box.space.t2, nil)
        check_triggers_def({})

        -- Test target tables restricts.
        box.execute("CREATE TABLE t1(a INT PRIMARY KEY,b INT);")
        space_id = box.space.t1.id

        sql = [[CREATE TRIGGER t1t INSTEAD OF UPDATE ON t1 FOR EACH ROW
                BEGIN DELETE FROM t1 WHERE a = old.a + 2; END;]]
        tuple = {"t1t", space_id, {sql = sql}}
        exp_err = {
            details = "cannot create INSTEAD OF trigger on space: t1",
            message = "Failed to execute SQL statement: "..
                      "cannot create INSTEAD OF trigger on space: t1",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
        space_id = box.space.v1.id

        sql = [[CREATE TRIGGER v1t BEFORE UPDATE ON v1 FOR EACH ROW
                BEGIN DELETE FROM t1 WHERE a = old.a + 2; END;]]
        tuple = {"v1t", space_id, {sql = sql}}
        exp_err = {
            details = "cannot create BEFORE trigger on view: v1",
            message = "Failed to execute SQL statement: "..
                      "cannot create BEFORE trigger on view: v1",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        sql = [[CREATE TRIGGER v1t AFTER UPDATE ON v1 FOR EACH ROW
                BEGIN DELETE FROM t1 WHERE a = old.a + 2; END;]]
        tuple = {"v1t", space_id, {sql = sql}}
        exp_err = {
            details = "cannot create AFTER trigger on view: v1",
            message = "Failed to execute SQL statement: "..
                      "cannot create AFTER trigger on view: v1",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        space_id =  box.space._fk_constraint.id
        sql = [[CREATE TRIGGER t1t INSTEAD OF UPDATE ON "_fk_constraint"
                FOR EACH ROW BEGIN DELETE FROM t1 WHERE a = old.a + 2; END;]]
        tuple = {"t1t", space_id, {sql =  sql}}
        exp_err = {
            details = "cannot create trigger on system table",
            message = "Failed to execute SQL statement: "..
                      "cannot create trigger on system table",
            name = "SQL_EXECUTE",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.v1, nil)
        t.assert_equals(box.space.t1, nil)

        --
        -- Check that FOR EACH ROW clause is mandatory
        --
        box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT);]])

        exp_err = "At line 1 at or near position 39: FOR EACH STATEMENT "..
                  "triggers are not implemented, please "..
                  "supply FOR EACH ROW clause"
        sql = "CREATE TRIGGER tr1 AFTER INSERT ON t1 BEGIN; END;"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
    end)
end

--
-- gh-3536: Some triggers cause error messages and/or half-finished updates
--
 g.test_3536_some_triggers_cause_error_messages = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t (
            s1 INT,
            s2 INT,
            s3 INT,
            s4 INT PRIMARY KEY);
        ]])
        box.execute([[CREATE VIEW v AS SELECT s1, s2 FROM t;]])
        box.execute([[CREATE TRIGGER tv INSTEAD OF UPDATE ON v
                      FOR EACH ROW BEGIN UPDATE t SET
                      s3 = new.s1 WHERE s1 = old.s1; END;]])
        box.execute("INSERT INTO t VALUES (1,1,1,1);")

        box.execute("UPDATE v SET s2 = s1 + 1;")
        box.execute("UPDATE v SET s1 = s1 + 5;")
        local exp = {
            metadata = {
                 {
                    name = "s1",
                    type = "integer",
                 },
                 {
                    name = "s2",
                    type = "integer",
                 },
                 {
                    name = "s3",
                    type = "integer",
                 },
                 {
                    name = "s4",
                    type = "integer",
                 },
            },
            rows = {{1, 1, 6, 1}},
        }
        t.assert_equals(box.execute("SELECT * FROM t;"), exp)

        box.execute("DROP VIEW v;")
        box.execute("DROP TABLE t;")
        t.assert_equals(box.space.v, nil)
        t.assert_equals(box.space.t, nil)
    end)
end

--
-- gh-3653: Disallow bindings for DDL
--
 g.test_3653_disallow_bindings_for_ddl = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE t1(a INT PRIMARY KEY, b INT);]])
        local space_id = box.space.t1.id
        local _trigger = box.space._trigger
        local sql = "CREATE TRIGGER tr1 AFTER INSERT ON t1 "..
                    "FOR EACH ROW WHEN new.a = ? BEGIN SELECT 1; END;"
        local _, err = box.execute(sql)
        local exp_err = "At line 1 at or near position 67: "..
                    "bindings are not allowed in DDL"
        t.assert_equals(tostring(err), exp_err)

        sql = "CREATE TRIGGER tr1 AFTER INSERT ON t1 "..
              "FOR EACH ROW WHEN new.a = ? BEGIN SELECT 1; END;"
        local tuple = {"TR1", space_id, {sql = sql}}
        exp_err = {
            details = "bindings are not allowed in DDL",
            message = "At line 1 at or near position 67: "..
                      "bindings are not allowed in DDL",
            name = "SQL_PARSER_GENERIC_WITH_POS",
        }
        t.assert_error_covers(exp_err, _trigger.insert, _trigger, tuple)

        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
    end)
end

--
-- gh-3570: Use box_space_id_by_name() instead of schema_find_id()
-- in SQL
--
 g.test_3570_create_trigger_not_executed_if_not_enough_rights = function(cg)
    cg.server:exec(function()
        box.schema.user.create('tester')
        box.session.su('admin', box.schema.user.grant, 'tester',
                       'read,write,create', 'space', '_trigger')
        box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY AUTOINCREMENT);")
        box.session.su('tester')
        --
        -- Ensure that the CREATE TRIGGER statement cannot be executed if
        -- the user does not have enough rights. In this case, the user
        -- does not have rights to read from _space.
        --
        local exp_err = [[Space 't1' does not exist]]
        local sql = "CREATE TRIGGER r1 AFTER INSERT ON t1 "..
                    "FOR EACH ROW BEGIN SELECT 1; END;"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        box.session.su('admin')
        box.schema.user.drop('tester')
        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
    end)
end

--
-- gh-4188: make sure that the identifiers that were generated
-- during the INSERT performed by the triggers are not returned.
--
 g.test_4188_do_not_show_ids_generated_by_triggers = function(cg)
    cg.server:exec(function()
        box.execute('CREATE TABLE t1 (i INT PRIMARY KEY AUTOINCREMENT);')
        box.execute('CREATE TABLE t2 (i INT PRIMARY KEY AUTOINCREMENT);')
        box.execute('CREATE TABLE t3 (i INT PRIMARY KEY AUTOINCREMENT);')
        local sql = "CREATE TRIGGER r1 AFTER INSERT ON t2 "..
                    "FOR EACH ROW BEGIN INSERT INTO t1 VALUES (NULL); END;"
        box.execute(sql)
        box.execute('INSERT INTO t1 VALUES (100);')
        box.execute('INSERT INTO t2 VALUES (NULL), (NULL), (NULL);')
        local exp = {
            metadata = {
                 {
                    name = "i",
                    type = "integer",
                 },
            },
            rows = {{100}, {101}, {102}, {103}},
        }
        t.assert_equals(box.execute('SELECT * FROM t1;'), exp)

        exp = {
            metadata = {
                {
                    name = "i",
                    type = "integer",
                },
            },
            rows = {{1}, {2}, {3}},
        }
        t.assert_equals(box.execute('SELECT * FROM t2;'), exp)

        sql = "CREATE TRIGGER r2 AFTER INSERT ON t3 "..
              "FOR EACH ROW BEGIN INSERT INTO t2 VALUES (NULL); END;"
        box.execute(sql)
        box.execute('INSERT INTO t3 VALUES (NULL), (NULL), (NULL);')
        exp = {
            metadata = {
                {
                    name = "i",
                    type = "integer",
                },
            },
            rows = {{100}, {101}, {102}, {103}, {104}, {105}, {106}},
        }
        t.assert_equals(box.execute('SELECT * FROM t1;'), exp)

        exp = {
            metadata = {
                {
                    name = "i",
                    type = "integer",
                },
            },
            rows = {{1}, {2}, {3}, {4}, {5}, {6}},
        }
        t.assert_equals(box.execute('SELECT * FROM t2;'), exp)

        exp = {
            metadata = {
                {
                    name = "i",
                    type = "integer",
                },
            },
            rows = {{1}, {2}, {3}},
        }
        t.assert_equals(box.execute('SELECT * FROM t3;'), exp)

        box.execute('DROP TABLE t1;')
        box.execute('DROP TABLE t2;')
        box.execute('DROP TABLE t3;')
        t.assert_equals(box.space.t1, nil)
        t.assert_equals(box.space.t2, nil)
        t.assert_equals(box.space.t3, nil)
    end)
end

--
-- gh-2141 delete trigger drop table
--
 g.test_2141_delete_trigger_drop_table = function(cg)
    cg.server:exec(function()
        -- create space
        box.execute("CREATE TABLE t(id INT PRIMARY KEY);")
        local sql = "CREATE TRIGGER tt_bu BEFORE UPDATE ON t "..
                    "FOR EACH ROW BEGIN SELECT 1; END;"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_au AFTER UPDATE ON t "..
              "FOR EACH ROW BEGIN SELECT 1; END;"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_bi BEFORE INSERT ON t "..
              "FOR EACH ROW BEGIN SELECT 1; END;"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_ai AFTER INSERT ON t "..
              "FOR EACH ROW BEGIN SELECT 1; END;"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_bd BEFORE DELETE ON t "..
              "FOR EACH ROW BEGIN SELECT 1; END;"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_ad AFTER DELETE ON t "..
              "FOR EACH ROW BEGIN SELECT 1; END;"
        box.execute(sql)

        -- check that these triggers exist
        local exp = {
            metadata = {
                {
                    name = "name",
                    type = "string",
                },
                {
                    name = "opts",
                    type = "map",
                },
            },
            rows = {
                {
                    "tt_ad",
                    {
                        sql = "CREATE TRIGGER tt_ad AFTER DELETE ON t "..
                              "FOR EACH ROW BEGIN SELECT 1; END;",
                    },
                },
                {
                    "tt_ai",
                    {
                        sql = "CREATE TRIGGER tt_ai AFTER INSERT ON t "..
                              "FOR EACH ROW BEGIN SELECT 1; END;",
                    },
                },
                {
                    "tt_au",
                    {
                        sql = "CREATE TRIGGER tt_au AFTER UPDATE ON t "..
                              "FOR EACH ROW BEGIN SELECT 1; END;",
                    },
                },
                {
                    "tt_bd",
                    {
                        sql = "CREATE TRIGGER tt_bd BEFORE DELETE ON t "..
                              "FOR EACH ROW BEGIN SELECT 1; END;",
                    },
                },
                {
                    "tt_bi",
                    {
                        sql = "CREATE TRIGGER tt_bi BEFORE INSERT ON t "..
                              "FOR EACH ROW BEGIN SELECT 1; END;",
                    },
                },
                {
                    "tt_bu",
                    {
                        sql = "CREATE TRIGGER tt_bu BEFORE UPDATE ON t "..
                              "FOR EACH ROW BEGIN SELECT 1; END;",
                    },
                },
            },
        }
        local res = box.execute([[SELECT "name","opts" FROM "_trigger";]])
        t.assert_equals(res, exp)

        -- drop table
        box.execute("DROP TABLE t")
        t.assert_equals(box.space.t, nil)

        -- check that triggers were dropped with deleted table
        exp = {
            metadata = {
                {
                    name = "name",
                    type = "string",
                },
                {
                    name = "opts",
                    type = "map",
                },
            },
            rows = {},
        }
        res = box.execute([[SELECT "name", "opts" FROM "_trigger";]])
        t.assert_equals(res, exp)
    end)
end

g = t.group("gh-3531")

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- gh-3531: Assertion with trigger and two storage engines
--
 g.test_3531_assertion_with_triggers = function(cg)
    cg.server:exec(function()
        box.execute([[CREATE TABLE m (
            s0 INT PRIMARY KEY,
            s1 TEXT UNIQUE) WITH ENGINE = 'vinyl';
        ]])
        box.execute([[CREATE TRIGGER m1 BEFORE UPDATE ON m FOR EACH ROW BEGIN
                      UPDATE n SET s2 = 'now'; END;]])

        box.execute([[CREATE TABLE n (
            s0 INT PRIMARY KEY,
            s1 TEXT UNIQUE,
            s2 NUMBER) WITH ENGINE = 'memtx';
        ]])
        box.execute("INSERT INTO m VALUES (0, '0');")
        box.execute("INSERT INTO n VALUES (0, '', NULL);")
        local exp_err = "MVCC is unavailable for storage engine 'memtx' "..
                        "so it cannot be used in the same transaction "..
                        "with 'vinyl', which supports MVCC"
        local _, err = box.execute("UPDATE m SET s1 = 'The Rain In Spain';")
        t.assert_equals(tostring(err), exp_err)

        box.execute("DROP TABLE m;")
        box.execute("DROP TABLE n;")
        t.assert_equals(box.space.m, nil)
        t.assert_equals(box.space.n, nil)

        -- Case 2: Src 'memtx' table; Dst 'vinyl' table
        box.execute([[CREATE TABLE m (
            s0 INT PRIMARY KEY,
            s1 TEXT UNIQUE)  WITH ENGINE = 'memtx';
        ]])
        box.execute([[CREATE TRIGGER m1 BEFORE UPDATE ON m FOR EACH ROW BEGIN
                      UPDATE n SET s2 = 'now'; END;]])
        box.execute([[CREATE TABLE n (
            s0 INT PRIMARY KEY,
            s1 TEXT UNIQUE,
            s2 NUMBER) WITH ENGINE = 'vinyl';
        ]])
        box.execute("INSERT INTO m VALUES (0, '0');")
        box.execute("INSERT INTO n VALUES (0, '', NULL);")
        _, err = box.execute("UPDATE m SET s1 = 'The Rain In Spain';")
        t.assert_equals(tostring(err), exp_err)

        box.execute("DROP TABLE m;")
        box.execute("DROP TABLE n;")
        t.assert_equals(box.space.m, nil)
        t.assert_equals(box.space.n, nil)

        -- Test SQL Transaction with LUA
        box.execute([[CREATE TABLE test (
            id INT PRIMARY KEY) WITH ENGINE = 'memtx';
        ]])
        box.execute([[CREATE TABLE test2 (
            id INT PRIMARY KEY) WITH ENGINE = 'vinyl';
        ]])
        box.execute("INSERT INTO test VALUES (2);")
        box.execute("START TRANSACTION;")
        box.execute("INSERT INTO test2 VALUES (1);")
        _, err = box.execute("SELECT * FROM test;")
        t.assert_equals(tostring(err), exp_err)

        box.execute("ROLLBACK;")
        box.execute("DROP TABLE test;")
        box.execute("DROP TABLE test2;")
        t.assert_equals(box.space.test, nil)
        t.assert_equals(box.space.test2, nil)
    end)
end
