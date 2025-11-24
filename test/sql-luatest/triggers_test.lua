local server = require('luatest.server')
local t = require('luatest')

local g = t.group("gh-test", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[set session "sql_default_engine" = '%s']], {engine})
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = false;]])
    end)
    cg.server:drop()
end)

--
-- gh-3273: Move Triggers to server
--
 g.test_triggers = function(cg)
    cg.server:exec(function()
        -- Get invariant part of the tuple; name and opts don't change.
        local function immutable_part(data)
            local r = {}
            for _, l in pairs(data) do
                table.insert(r, {l.name, l.opts})
            end
            return r
        end

        box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
        box.execute("CREATE TABLE t2(x INTEGER PRIMARY KEY);")

        box.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN
                      INSERT INTO t2 VALUES(1); END; ]])
        immutable_part(box.space._trigger:select())

        local space_id = box.space._space.index["name"]:get('t1').id

        -- Checks for LUA tuples.
        local sql = [[CREATE TRIGGER t1t AFTER INSERT ON t1
                      FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;]]
        local tuple = {"T1t", space_id, {sql = sql}}
        local _, err = pcall(function()
                                   return box.space._trigger:insert(tuple)
                               end)
        local exp = "Failed to execute SQL statement: "..
                    "trigger name does not match extracted from SQL"
        t.assert_equals(tostring(err), exp)

        sql = [[CREATE TRIGGER t12t AFTER INSERT ON t1
                FOR EACH ROW BEGIN INSERT INTO t2 VALUES(1); END;]]
        tuple = {"T1t", space_id, {sql = sql}}
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        t.assert_equals(tostring(err), exp)

        sql = [[CREATE TRIGGER t2t AFTER
                INSERT ON t1 FOR EACH ROW BEGIN
                INSERT INTO t2 VALUES(1); END;]]
        tuple = {"t2t", box.space.t1.id + 1, {sql = sql}}
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        exp = "Failed to execute SQL statement: trigger space_id does "..
               "not match the value resolved on AST building from SQL"
        t.assert_equals(tostring(err), exp)

        immutable_part(box.space._trigger:select())

        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
        immutable_part(box.space._trigger:select())

        box.execute("CREATE TABLE t1(x INTEGER PRIMARY KEY);")
        box.execute([[CREATE TRIGGER t1t AFTER INSERT ON t1 FOR EACH ROW BEGIN
                      INSERT INTO t2 VALUES(1); END; ]])
        immutable_part(box.space._trigger:select())

        space_id = box.space._space.index["name"]:get('t1').id

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
        sql = [[CREATE TRIGGER t2t AFTER INSERT ON t1 FOR
                EACH ROW BEGIN INSERT INTO t2 VALUES(2); END;]]
        tuple = {"t2t", space_id, {sql = sql}}
        _ = box.space._trigger:insert(tuple)
        sql = [[CREATE TRIGGER t3t AFTER INSERT ON t1 FOR
                EACH ROW BEGIN INSERT INTO t2 VALUES(3); END;]]
        tuple = {"t3t", space_id, {sql = sql}}
        _ = box.space._trigger:insert(tuple)
        immutable_part(box.space._trigger:select())
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
        _ = box.space._trigger:delete("t3t")
        immutable_part(box.space._trigger:select())
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
        box.execute([[CREATE TRIGGER t2t AFTER INSERT ON t1 FOR EACH ROW BEGIN
                      INSERT INTO t2 VALUES(2); END; ]])
        box.execute([[CREATE TRIGGER t3t AFTER INSERT ON t1 FOR EACH ROW BEGIN
                      INSERT INTO t2 VALUES(3); END; ]])
        immutable_part(box.space._trigger:select())
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
        immutable_part(box.space._trigger:select())

        -- Test target tables restricts.
        box.execute("CREATE TABLE t1(a INT PRIMARY KEY,b INT);")
        space_id = box.space.t1.id

        sql = [[create trigger t1t instead of update on
                t1 for each row begin delete from t1 WHERE
                a=old.a+2; end;]]
        tuple = {"t1t", space_id, {sql = sql}}
        exp = "Failed to execute SQL statement: "..
               "cannot create INSTEAD OF trigger on space: t1"
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        t.assert_equals(tostring(err), exp)

        box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
        space_id = box.space.v1.id

        sql = [[create trigger v1t before update on v1
                for each row begin delete from t1 WHERE
                a=old.a+2; end;]]
        tuple = {"v1t", space_id, {sql = sql}}
        exp = "Failed to execute SQL statement: "..
              "cannot create BEFORE trigger on view: v1"
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        t.assert_equals(tostring(err), exp)
        sql = [[create trigger v1t AFTER update on v1
               for each row begin delete from t1 WHERE
               a=old.a+2; end;]]
        tuple = {"v1t", space_id, {sql = sql}}
        exp = "Failed to execute SQL statement: "..
              "cannot create AFTER trigger on view: v1"
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        t.assert_equals(tostring(err), exp)

        space_id =  box.space._fk_constraint.id
        sql = [[create trigger t1t instead of update on "_fk_constraint"
                for each row begin delete from t1 WHERE a=old.a+2; end;]]
        tuple = {"t1t", space_id, {sql =  sql}}
        exp = "Failed to execute SQL statement: "..
              "cannot create trigger on system table"
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        t.assert_equals(tostring(err), exp)

        box.execute("DROP VIEW v1;")
        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.v1, nil)
        t.assert_equals(box.space.t1, nil)

    end)
end

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
        box.execute("INSERT INTO n VALUES (0, '',null);")
        local exp = "MVCC is unavailable for storage engine 'memtx'"..
                    " so it cannot be used in the same transaction "..
                    "with 'vinyl', which supports MVCC"
        local _, err = box.execute("UPDATE m SET s1 = 'The Rain In Spain';")
        t.assert_equals(tostring(err), exp)

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
        box.execute("INSERT INTO n VALUES (0, '',null);")
        exp = "MVCC is unavailable for storage engine 'memtx'"..
              " so it cannot be used in the same transaction "..
              "with 'vinyl', which supports MVCC"
        _, err = box.execute("UPDATE m SET s1 = 'The Rain In Spain';")
        t.assert_equals(tostring(err), exp)

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
        box.execute("INSERT INTO test VALUES (2)")
        box.execute("START TRANSACTION")
        box.execute("INSERT INTO test2 VALUES (1)")
        exp = "MVCC is unavailable for storage engine 'memtx'"..
              " so it cannot be used in the same transaction"..
              " with 'vinyl', which supports MVCC"
        _, err = box.execute("SELECT * FROM test")
        t.assert_equals(tostring(err), exp)

        box.execute("ROLLBACK;")
        box.execute("DROP TABLE test;")
        box.execute("DROP TABLE test2;")
        t.assert_equals(box.space.test, nil)
        t.assert_equals(box.space.test2, nil)
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
        box.execute("SELECT * FROM t;")

        box.execute("UPDATE v SET s2 = s1 + 1;")
        box.execute("UPDATE v SET s1 = s1 + 5;")
        local exp = {
            metadata = {
                 {
                    name = "s1",
                    type = "integer"
                 },
                 {
                    name = "s2",
                    type = "integer"
                 },
                 {
                    name = "s3",
                    type = "integer"
                 },
                 {
                    name = "s4",
                    type = "integer"
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
        box.execute([[CREATE TABLE t1(
            a INT PRIMARY KEY,
            b INT);
        ]])
        local space_id = box.space.t1.id
        local sql = "CREATE TRIGGER tr1 AFTER INSERT ON"..
                    " t1 FOR EACH ROW WHEN new.a = ? "..
                    "BEGIN SELECT 1; END;"
        local _, err = box.execute(sql)
        local exp = "At line 1 at or near position 67:"..
                    " bindings are not allowed in DDL"
        t.assert_equals(tostring(err), exp)
        sql = "CREATE TRIGGER tr1 AFTER INSERT ON t1"..
              " FOR EACH ROW WHEN new.a = ? BEGIN SELECT 1; END;"
        local tuple = {"TR1", space_id, {sql = sql}}
        _, err = pcall(function()
                             return box.space._trigger:insert(tuple)
                         end)
        t.assert_equals(tostring(err), exp)
        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)

        --
        -- Check that FOR EACH ROW clause is moandatory
        --
        box.execute([[CREATE TABLE t1(
            a INT PRIMARY KEY,
            b INT);
        ]])

        exp = "At line 1 at or near position 39: FOR EACH STATEMENT"..
              " triggers are not implemented, please"..
              " supply FOR EACH ROW clause"
        local sql = "CREATE TRIGGER tr1 AFTER INSERT ON t1 BEGIN ; END;"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp)

        box.execute("DROP TABLE t1;")
        t.assert_equals(box.space.t1, nil)
    end)
end

--
-- gh-3570: Use box_space_id_by_name() instead of schema_find_id()
-- in SQL
--
 g.test_3570 = function(cg)
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
        local exp = [[Space 't1' does not exist]]
        local sql = "CREATE TRIGGER r1 AFTER INSERT ON t1 "..
                    "FOR EACH ROW BEGIN SELECT 1; END;"
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp)

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
 g.test_4188 = function(cg)
    cg.server:exec(function()
        box.execute([[set session "sql_default_engine" = 'vinyl']])
        box.execute('CREATE TABLE t1 (i INT PRIMARY KEY AUTOINCREMENT);')
        box.execute('CREATE TABLE t2 (i INT PRIMARY KEY AUTOINCREMENT);')
        box.execute('CREATE TABLE t3 (i INT PRIMARY KEY AUTOINCREMENT);')
        local sql = "CREATE TRIGGER r1 AFTER INSERT ON t2"..
                    " FOR EACH ROW BEGIN INSERT INTO t1 VALUES (null); END"
        box.execute(sql)
        box.execute('INSERT INTO t1 VALUES (100);')
        box.execute('INSERT INTO t2 VALUES (NULL), (NULL), (NULL);')
        local exp = {
            metadata = {
                 {
                    name = "i",
                    type = "integer"
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
        sql = "CREATE TRIGGER r2 AFTER INSERT ON t3"..
              " FOR EACH ROW BEGIN INSERT INTO t2 VALUES (null); END"
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
        box.execute("CREATE TABLE t(id INT PRIMARY KEY)")
        local sql = "CREATE TRIGGER tt_bu BEFORE UPDATE ON t"..
                    " FOR EACH ROW BEGIN SELECT 1; END"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_au AFTER UPDATE ON t"..
              " FOR EACH ROW BEGIN SELECT 1; END"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_bi BEFORE INSERT ON t"..
              " FOR EACH ROW BEGIN SELECT 1; END"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_ai AFTER INSERT ON t"..
              " FOR EACH ROW BEGIN SELECT 1; END"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_bd BEFORE DELETE ON t"..
              " FOR EACH ROW BEGIN SELECT 1; END"
        box.execute(sql)
        sql = "CREATE TRIGGER tt_ad AFTER DELETE ON t"..
              " FOR EACH ROW BEGIN SELECT 1; END"
        box.execute(sql)

        -- check that these triggers exist
        local exp = {
            metadata = {
                {
                    name = "name",
                    type = "string"
                },
                {
                    name = "opts",
                    type = "map"
                },
            },
            rows = {
                {
                    "tt_ad",
                    {
                        sql = "CREATE TRIGGER tt_ad AFTER DELETE ON t"..
                              " FOR EACH ROW BEGIN SELECT 1; END"
                    },
                },
                {
                    "tt_ai",
                    {
                        sql = "CREATE TRIGGER tt_ai AFTER INSERT ON t"..
                              " FOR EACH ROW BEGIN SELECT 1; END"
                    },
                },
                {
                    "tt_au",
                    {
                        sql = "CREATE TRIGGER tt_au AFTER UPDATE ON t"..
                              " FOR EACH ROW BEGIN SELECT 1; END"
                    },
                },
                {
                    "tt_bd",
                    {
                        sql = "CREATE TRIGGER tt_bd BEFORE DELETE ON t"..
                              " FOR EACH ROW BEGIN SELECT 1; END"
                    },
                },
                {
                    "tt_bi",
                    {
                        sql = "CREATE TRIGGER tt_bi BEFORE INSERT ON t"..
                              " FOR EACH ROW BEGIN SELECT 1; END"
                    },
                },
                {
                    "tt_bu",
                    {
                        sql = "CREATE TRIGGER tt_bu BEFORE UPDATE ON t"..
                              " FOR EACH ROW BEGIN SELECT 1; END"
                    },
                }
            }
        }
        t.assert_equals(box.execute([[SELECT "name",
                                      "opts" FROM "_trigger"]]), exp)

        -- drop table
        box.execute("DROP TABLE t")
        t.assert_equals(box.space.t, nil)

        -- check that triggers were dropped with deleted table
        box.execute([[SELECT "name", "opts" FROM "_trigger"]])
    end)
end
