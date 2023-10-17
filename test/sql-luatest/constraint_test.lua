local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'constraints'})
    g.server:start()

    local data_dir = 'test/sql-luatest/upgrade/2.10.0'
    g.upgrade = server:new({alias = 'upgrade', datadir = data_dir})
    g.upgrade:start()
    g.upgrade:exec(function()
        box.schema.upgrade()
    end)
end)

g.after_all(function()
    g.server:stop()
    g.upgrade:stop()
end)

-- Make sure ALTER TABLE ADD COLUMN does not drop field constraints.
g.test_constraints_1 = function()
    g.server:exec(function()
        local fmt = {{'a', 'integer'}, {'b', 'integer'}}

        local body = "function(x) return true end"
        box.schema.func.create('ck1', {is_deterministic = true, body = body})
        local func_id = box.func.ck1.id
        fmt[1].constraint = {ck = 'ck1'}

        local s0 = box.schema.space.create('a', {format = fmt})
        local fk = {one = {field = 'a'}, two = {space = s0.id, field = 'b'}}
        fmt[2].foreign_key = fk

        local s = box.schema.space.create('b', {format = fmt})
        t.assert_equals(s:format()[1].constraint, {ck = func_id})
        t.assert_equals(s:format()[2].foreign_key, fk)
        box.execute([[ALTER TABLE "b" ADD COLUMN c INT;]])
        t.assert_equals(s:format()[1].constraint, {ck = func_id})
        t.assert_equals(s:format()[2].foreign_key, fk)
        box.space.b:drop()
        box.space.a:drop()
        box.schema.func.drop('ck1')
    end)
end

-- Make sure ALTER TABLE DROP CONSTRAINT drops field and tuple constraints.
g.test_constraints_2 = function()
    g.server:exec(function()
        local body = "function(x) return true end"
        box.schema.func.create('ck1', {is_deterministic = true, body = body})
        local func_id = box.space._func.index[2]:get{'ck1'}.id

        local fk0 = {one = {field = {a = 'a'}}, two = {field = {b = 'b'}}}
        local ck0 = {three = 'ck1', four = 'ck1'}
        local fk1 = {five = {field = 'a'}, six = {field = 'b'}}
        local ck1 = {seven = 'ck1', eight = 'ck1'}

        local fmt = {{'a', 'integer'}, {'b', 'integer'}}
        fmt[1].constraint = ck1
        fmt[2].foreign_key = fk1

        local def = {format = fmt, foreign_key = fk0, constraint = ck0}
        local s = box.schema.space.create('a', def)
        ck0.three = func_id
        ck0.four = func_id
        ck1.seven = func_id
        ck1.eight = func_id
        t.assert_equals(s.foreign_key, fk0)
        t.assert_equals(s.constraint, ck0)
        t.assert_equals(s:format()[1].constraint, ck1)
        t.assert_equals(s:format()[2].foreign_key, fk1)

        local ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "one";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, {two = {field = {b = 'b'}}})
        t.assert_equals(s.constraint, ck0)
        t.assert_equals(s:format()[1].constraint, ck1)
        t.assert_equals(s:format()[2].foreign_key, fk1)

        local _, err = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "one";]])
        local res = [[Constraint 'one' does not exist in space 'a']]
        t.assert_equals(err.message, res)

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "four";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, {two = {field = {b = 'b'}}})
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, ck1)
        t.assert_equals(s:format()[2].foreign_key, fk1)

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "a"."seven";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, {two = {field = {b = 'b'}}})
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, fk1)

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "two";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, fk1)

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "b"."five";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "a"."eight";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {})
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "three";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, {})
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "b"."six";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, {})
        t.assert_equals(s:format()[2].foreign_key, {})

        _, err = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "eight";]])
        res = [[Constraint 'eight' does not exist in space 'a']]
        t.assert_equals(err.message, res)

        box.space.a:drop()
        box.schema.func.drop('ck1')
    end)
end

--
-- Make sure "reference trigger action", "constraint check time" and "match
-- type" rules are disabled.
--
g.test_constraints_3 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT REFERENCES t ]]..
                    [[DEFERRABLE);]]
        local _, err = box.execute(sql);
        local res = [[Syntax error at line 1 near 'DEFERRABLE']]
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT REFERENCES t ]]..
              [[DEFERRABLE INITIALLY DEFERRED);]]
        _, err = box.execute(sql);
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT REFERENCES t ]]..
              [[MATCH FULL);]]
        _, err = box.execute(sql);
        res = [[At line 1 at or near position 54: keyword 'MATCH' is ]]..
              [[reserved. Please use double quotes if 'MATCH' is an ]]..
              [[identifier.]]
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT REFERENCES t ]]..
              [[ON UPDATE SET DEFAULT);]]
        _, err = box.execute(sql);
        res = [[At line 1 at or near position 54: keyword 'ON' is reserved. ]]..
              [[Please use double quotes if 'ON' is an identifier.]]
        t.assert_equals(err.message, res)

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT REFERENCES t ]]..
              [[ON DELETE SET NULL);]]
        _, err = box.execute(sql);
        res = [[At line 1 at or near position 54: keyword 'ON' is reserved. ]]..
              [[Please use double quotes if 'ON' is an identifier.]]
        t.assert_equals(err.message, res)
    end)
end

-- Make sure "sql_defer_foreign_keys" session setting no longer exists.
g.test_constraints_4 = function()
    g.server:exec(function()
        local sql = [[SELECT * FROM "_session_settings" ]]..
                    [[WHERE "name" = 'sql_defer_foreign_keys';]]
        t.assert_equals(box.execute(sql).rows, {})

        sql = [[SET SESSION "sql_defer_foreign_keys" = TRUE;]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message,
            [[Session setting sql_defer_foreign_keys doesn't exist]])
    end)
end

-- Make sure field foreign key created properly.
g.test_constraints_5 = function()
    g.server:exec(function()
        local sql = "CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t(i));"
        box.execute(sql)
        local res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t);]]
        box.execute(sql)
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t(a));]]
        box.execute(sql)
        res = {fk_unnamed_T_A_1 = {field = 2, space = box.space.T.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY REFERENCES t(a), a INT);]]
        box.execute(sql)
        res = {fk_unnamed_T_I_1 = {field = 2, space = box.space.T.id}}
        t.assert_equals(box.space.T:format()[1].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, ]]..
              [[a INT CONSTRAINT one REFERENCES t);]]
        box.execute(sql)
        res = {ONE = {field = 1, space = box.space.T.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t0 (i INT PRIMARY KEY, a INT);]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t0(i));]]
        box.execute(sql)
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T0.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t0);]]
        box.execute(sql)
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T0.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t0(a));]]
        box.execute(sql)
        res = {fk_unnamed_T_A_1 = {field = 2, space = box.space.T0.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY REFERENCES t0(a), a INT);]]
        box.execute(sql)
        res = {fk_unnamed_T_I_1 = {field = 2, space = box.space.T0.id}}
        t.assert_equals(box.space.T:format()[1].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, ]]..
              [[a INT CONSTRAINT one REFERENCES t0);]]
        box.execute(sql)
        res = {ONE = {field = 1, space = box.space.T0.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT REFERENCES t(i);]]
        box.execute(sql)
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T.id}}
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT REFERENCES t;]]
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT REFERENCES t(a);]]
        res = {fk_unnamed_T_A_1 = {field = 2, space = box.space.T.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT CONSTRAINT one REFERENCES t;]]
        res = {ONE = {field = 1, space = box.space.T.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT REFERENCES t0(i);]]
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T0.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT REFERENCES t0;]]
        res = {fk_unnamed_T_A_1 = {field = 1, space = box.space.T0.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT REFERENCES t0(a);]]
        res = {fk_unnamed_T_A_1 = {field = 2, space = box.space.T0.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        sql = [[ALTER TABLE t ADD COLUMN a INT CONSTRAINT one REFERENCES t0;]]
        res = {ONE = {field = 1, space = box.space.T0.id}}
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.schema.space.create('T1')
        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t1);]]
        local _, err = box.execute(sql)
        res = [[Failed to create foreign key constraint 'fk_unnamed_T_A_1': ]]..
              [[referenced space doesn't feature PRIMARY KEY]]
        t.assert_equals(err.message, res)
        box.space.T1:drop()

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT REFERENCES t(i, a));]]
        _, err = box.execute(sql)
        res = [[Failed to create foreign key constraint 'fk_unnamed_T_A_1': ]]..
              [[number of columns in foreign key does not match the number ]]..
              [[of columns in the primary index of referenced table]]
        t.assert_equals(err.message, res)

        box.execute([[CREATE VIEW v AS SELECT * FROM t0;]])
        sql = [[CREATE TABLE t (a INT REFERENCES v(i), i INT PRIMARY KEY);]]
        _, err = box.execute(sql)
        res = [[Failed to create foreign key constraint 'fk_unnamed_T_A_1': ]]..
              [[referenced space can't be VIEW]]
        t.assert_equals(err.message, res)
        box.execute([[DROP VIEW v;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT ]]..
              [[CONSTRAINT fk1 REFERENCES t(i) ]]..
              [[CONSTRAINT fk1 REFERENCES t(i));]]
        _, err = box.execute(sql)
        res = [[FOREIGN KEY constraint 'FK1' already exists in space 'T']]
        t.assert_equals(err.message, res)

        box.execute([[DROP TABLE t0;]])
    end)
end

-- Make sure tuple foreign key created properly.
g.test_constraints_6 = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT, ]]..
            [[FOREIGN KEY (i, a) REFERENCES t(i, a));]]
        box.execute(sql)
        local res = {fk_unnamed_T_1 = {space = box.space.T.id,
                                       field = {[1] = 1, [2] = 2}}}
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT, a INT, PRIMARY KEY(i, a), ]]..
            [[FOREIGN KEY (i, a) REFERENCES t);]]
        box.execute(sql)
        res = {fk_unnamed_T_1 = {space = box.space.T.id,
                                 field = {[1] = 1, [2] = 2}}}
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT, CONSTRAINT one ]]..
            [[FOREIGN KEY (i, a) REFERENCES t(i, a));]]
        box.execute(sql)
        res = {ONE = {space = box.space.T.id, field = {[1] = 1, [2] = 2}}}
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t0 (i INT, a INT, PRIMARY KEY (i, a));]])
        local space_id = box.space.T0.id

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT, ]]..
            [[FOREIGN KEY (i, a) REFERENCES t0(a, i));]]
        res = {fk_unnamed_T_1 = {space = space_id, field = {[1] = 2, [2] = 1}}}
        box.execute(sql)
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t (i INT PRIMARY KEY, a INT, ]]..
            [[FOREIGN KEY (i, a) REFERENCES t0);]]
        res = {fk_unnamed_T_1 = {space = space_id, field = {[1] = 1, [2] = 2}}}
        box.execute(sql)
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT, a INT, PRIMARY KEY (i, a))]])

        sql = [[ALTER TABLE t ADD CONSTRAINT c FOREIGN KEY (a) REFERENCES t(i)]]
        res = {C = {space = box.space.T.id, field = {[2] = 1}}}
        box.execute(sql)
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[ALTER TABLE t DROP CONSTRAINT c;]])

        sql = [[ALTER TABLE t ADD CONSTRAINT c FOREIGN KEY (a, i) REFERENCES t]]
        res = {C = {space = box.space.T.id, field = {[2] = 1, [1] = 2}}}
        box.execute(sql)
        t.assert_equals(box.space.T.foreign_key, res)
        box.execute([[ALTER TABLE t DROP CONSTRAINT c;]])

        box.execute([[DROP TABLE t;]])

        box.execute([[DROP TABLE t0;]])
    end)
end

-- Make sure field check constraints are created properly.
g.test_constraints_7 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT CHECK(a > 10));]])
        local res = {ck_unnamed_T_A_1 = box.func.check_T_ck_unnamed_T_A_1.id}
        t.assert_equals(box.space.T:format()[2].constraint, res)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_ck_unnamed_T_A_1:drop()

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY);]])
        box.execute([[ALTER TABLE t ADD COLUMN a INT CHECK(a > 10);]])
        res = {ck_unnamed_T_A_1 = box.func.check_T_ck_unnamed_T_A_1.id}
        t.assert_equals(box.space.T:format()[2].constraint, res)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_ck_unnamed_T_A_1:drop()

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY,
                    a INT CONSTRAINT one CHECK(a > 10));]])
        res = {ONE = box.func.check_T_ONE.id}
        t.assert_equals(box.space.T:format()[2].constraint, res)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_ONE:drop()

        local sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT CHECK(i > 10));]]
        local _, err = box.execute(sql)
        res = [[Failed to create check constraint 'ck_unnamed_T_A_1': ]]..
              [[wrong field name specified in the field check constraint]]
        t.assert_equals(err.message, res)
    end)
end

-- Make sure tuple check constraints are created properly.
g.test_constraints_8 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT,
                      CHECK(a > 10));]])
        local res = {ck_unnamed_T_1 = box.func.check_T_ck_unnamed_T_1.id}
        t.assert_equals(box.space.T.constraint, res)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_ck_unnamed_T_1:drop()

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY);]])
        box.execute([[ALTER TABLE t ADD CONSTRAINT two CHECK(a > 10);]])
        res = {TWO = box.func.check_T_TWO.id}
        t.assert_equals(box.space.T.constraint, res)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_TWO:drop()

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY,
                    a INT, CONSTRAINT one CHECK(a > 10));]])
        res = {ONE = box.func.check_T_ONE.id}
        t.assert_equals(box.space.T.constraint, res)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_ONE:drop()

        local sql = [[CREATE TABLE t(i INT PRIMARY KEY, a INT, CONSTRAINT ]]..
                    [[one CHECK(a > 10), CONSTRAINT one CHECK(a < 100));]]
        local _, err = box.execute(sql)
        res = [[Function for the check constraint 'ONE' with name ]]..
              [['check_T_ONE' already exists]]
        t.assert_equals(err.message, res)
        t.assert_equals(box.space._func.index[2]:get('check_T_ONE'), nil)
    end)
end

--
-- Make sure that the SQL foreign key and check constraints are correctly
-- converted to core foreign key and check constraints during the upgrade.
--
g.test_constraints_9 = function()
    g.upgrade:exec(function()
        t.assert_equals(box.space._ck_constraint:select(), {})
        t.assert_equals(box.space._fk_constraint:select(), {})

        local s = box.space._space.index[2]:get('T')
        local s1 = box.space._space.index[2]:get('T1')
        local s2 = box.space._space.index[2]:get('T2')
        t.assert_equals(s[6].constraint, nil)
        local fk1 = {fk_unnamed_T_1 = {field = {0}, space = s.id}}
        t.assert_equals(s[6].foreign_key, fk1)
        t.assert_equals(s1[6].constraint, nil)
        local fk2 = {fk_unnamed_T1_1 = {field = {0}, space = s.id},
            fk_unnamed_T1_2 = {field = {[0] = 1, [1] = 0}, space = s.id}}
        t.assert_equals(s1[6].foreign_key, fk2)
        local ck = {}
        ck.ck_unnamed_T2_1 = box.func.check_T2_ck_unnamed_T2_1.id
        ck.ck_unnamed_T2_2 = box.func.check_T2_ck_unnamed_T2_2.id
        ck.ck_unnamed_T2_3 = box.func.check_T2_ck_unnamed_T2_3.id
        t.assert_equals(s2[6].constraint, ck)
        t.assert_equals(s2[6].foreign_key, nil)
    end)
end

--
-- Make sure that check constraints can no longer be created through inserting
-- into _ck_constraint.
--
g.test_constraints_10 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY);]])
        local s = box.space.T
        local rec = {s.id, 'one', false, 'SQL', 'I > 10', true}
        t.assert_equals(box.space._ck_constraint:insert(rec), rec)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:insert({1}), {1})
        s:drop()
        box.space._ck_constraint:delete({s.id, 'one'})
    end)
end

-- Make sure that check constraints can no longer be ENABLED or DISABLED.
g.test_constraints_11 = function()
    g.server:exec(function()
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY,
                                     CONSTRAINT one CHECK (i > 10));]])
        local _, err = box.execute([[INSERT INTO t VALUES (1);]])
        t.assert_equals(err.message,
                        "Check constraint 'ONE' failed for a tuple")
        _, err = box.execute([[ALTER TABLE t DISABLE CHECK CONSTRAINT one;]])
        t.assert_equals(err.message, "Syntax error at line 1 near 'DISABLE'")
        _, err = box.execute([[INSERT INTO t VALUES (1);]])
        t.assert_equals(err.message,
                        "Check constraint 'ONE' failed for a tuple")
        _, err = box.execute([[ALTER TABLE t ENABLE CHECK CONSTRAINT one;]])
        t.assert_equals(err.message, "Syntax error at line 1 near 'ENABLE'")
        _, err = box.execute([[INSERT INTO t VALUES (1);]])
        t.assert_equals(err.message,
                        "Check constraint 'ONE' failed for a tuple")

        box.execute([[DROP TABLE t;]])
    end)
end

-- Make sure the field constraint can be dropped using
-- "ALTER TABLE table_name DROP CONSTRAINT field_name.constraint_name;"
-- and cannot be dropped using
-- "ALTER TABLE table_name DROP CONSTRAINT constraint_name;".
g.test_drop_field_constraints = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT PRIMARY KEY,
                      a INT CONSTRAINT b CHECK (a > 10));]]
        box.execute(sql)
        t.assert(box.space.T:format()[2].constraint['B'] ~= nil);
        local _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT b;]])
        local exp = "Constraint 'B' does not exist in space 'T'"
        t.assert_equals(err.message, exp)
        t.assert(box.space.T:format()[2].constraint['B'] ~= nil);

        local res = box.execute([[ALTER TABLE t DROP CONSTRAINT a.b;]])
        t.assert_equals(res, {row_count = 1})
        t.assert(box.space.T:format()[2].constraint['B'] == nil);

        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT a.b;]])
        exp = "Constraint 'A.B' does not exist in space 'T'"
        t.assert_equals(err.message, exp)
        box.execute([[DROP TABLE t;]])
        box.func.check_T_B:drop()
    end)
end

-- Make sure the ALTER TABLE DROP CONSTRAINT statement cannot drop constraints
-- if more than one constraint is found for a given name.
g.test_drop_constraints_with_the_same_name = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT CONSTRAINT c PRIMARY KEY,
                                      a INT CONSTRAINT b CHECK (a > 10)
                                      CONSTRAINT b REFERENCES t(i),
                                      CONSTRAINT c CHECK (i + a > 100));]]
        box.execute(sql)
        t.assert(box.space.T.constraint['C'] ~= nil);
        t.assert(box.space.T.index['C'] ~= nil);
        local _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT c;]])
        local exp = "Failed to execute SQL statement: "..
                    "ambiguous constraint name: 'C'"
        t.assert_equals(err.message, exp)
        t.assert(box.space.T.constraint['C'] ~= nil);
        t.assert(box.space.T.index['C'] ~= nil);

        t.assert(box.space.T:format()[2].constraint['B'] ~= nil);
        t.assert(box.space.T:format()[2].foreign_key['B'] ~= nil);
        _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT a.b;]])
        exp = "Failed to execute SQL statement: "..
              "ambiguous constraint name: 'A.B'"
        t.assert_equals(err.message, exp)
        t.assert(box.space.T:format()[2].constraint['B'] ~= nil);
        t.assert(box.space.T:format()[2].foreign_key['B'] ~= nil);
        box.execute([[DROP TABLE t;]])
        box.func.check_T_B:drop()
        box.func.check_T_C:drop()
    end)
end

-- Make sure the variations of the DROP CONSTRAINT statement with the specified
-- constraint type work correctly.
g.test_drop_constraints_with_type = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE t (i INT CONSTRAINT c PRIMARY KEY,
                                      a INT CONSTRAINT b CHECK (a > 10)
                                      CONSTRAINT b REFERENCES t(i),
                                      CONSTRAINT c CHECK (i + a > 100),
                                      CONSTRAINT c FOREIGN KEY (a) REFERENCES t,
                                      CONSTRAINT d UNIQUE(a));]]
        box.execute(sql)
        local s = box.space.T
        t.assert(s.constraint['C'] ~= nil);
        t.assert(s.foreign_key['C'] ~= nil);
        t.assert(s.index['C'] ~= nil);
        t.assert(s.index['D'] ~= nil);
        t.assert(s:format()[2].constraint['B'] ~= nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        -- Make sure that a constraint with the given name but with the wrong
        -- type will not be dropped.
        local _, err = box.execute([[ALTER TABLE t DROP CONSTRAINT d CHECK;]])
        local exp = "Constraint 'D' does not exist in space 'T'"
        t.assert_equals(err.message, exp)
        t.assert(s.constraint['C'] ~= nil);
        t.assert(s.foreign_key['C'] ~= nil);
        t.assert(s.index['C'] ~= nil);
        t.assert(s.index['D'] ~= nil);
        t.assert(s:format()[2].constraint['B'] ~= nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        box.execute([[ALTER TABLE t DROP CONSTRAINT d UNIQUE;]])
        t.assert(s.constraint['C'] ~= nil);
        t.assert(s.foreign_key['C'] ~= nil);
        t.assert(s.index['C'] ~= nil);
        t.assert(s.index['D'] == nil);
        t.assert(s:format()[2].constraint['B'] ~= nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        box.execute([[ALTER TABLE t DROP CONSTRAINT c CHECK;]])
        t.assert(s.constraint == nil or s.constraint['C'] == nil);
        t.assert(s.foreign_key['C'] ~= nil);
        t.assert(s.index['C'] ~= nil);
        t.assert(s.index['D'] == nil);
        t.assert(s:format()[2].constraint['B'] ~= nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        box.execute([[ALTER TABLE t DROP CONSTRAINT c FOREIGN KEY;]])
        t.assert(s.constraint == nil or s.constraint['C'] == nil);
        t.assert(s.foreign_key == nil or s.foreign_key['C'] == nil);
        t.assert(s.index['C'] ~= nil);
        t.assert(s.index['D'] == nil);
        t.assert(s:format()[2].constraint['B'] ~= nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        box.execute([[ALTER TABLE t DROP CONSTRAINT c PRIMARY KEY;]])
        t.assert(s.constraint == nil or s.constraint['C'] == nil);
        t.assert(s.foreign_key == nil or s.foreign_key['C'] == nil);
        t.assert(s.index['C'] == nil);
        t.assert(s.index['D'] == nil);
        t.assert(s:format()[2].constraint['B'] ~= nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        box.execute([[ALTER TABLE t DROP CONSTRAINT a.b CHECK;]])
        t.assert(s.constraint == nil or s.constraint['C'] == nil);
        t.assert(s.foreign_key == nil or s.foreign_key['C'] == nil);
        t.assert(s.index['C']  == nil);
        t.assert(s.index['D'] == nil);
        t.assert(s:format()[2].constraint['B'] == nil);
        t.assert(s:format()[2].foreign_key['B'] ~= nil);

        box.execute([[ALTER TABLE t DROP CONSTRAINT a.b FOREIGN KEY;]])
        t.assert(s.constraint == nil or s.constraint['C'] == nil);
        t.assert(s.foreign_key == nil or s.foreign_key['C'] == nil);
        t.assert(s.index['C']  == nil);
        t.assert(s.index['D'] == nil);
        t.assert(s:format()[2].constraint['B'] == nil);
        t.assert(s:format()[2].foreign_key['B'] == nil);

        box.execute([[DROP TABLE t;]])
        box.func.check_T_B:drop()
        box.func.check_T_C:drop()
    end)
end
