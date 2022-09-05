local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'constraints'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure ALTER TABLE ADD COLUMN does not drop field constraints.
g.test_constraints_1 = function()
    g.server:exec(function()
        local t = require('luatest')
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
        local t = require('luatest')

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

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "seven";]])
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

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "five";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, {eight = func_id})
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "eight";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, {three = func_id})
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "three";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:format()[2].foreign_key, {six = {field = 'b'}})

        ret = box.execute([[ALTER TABLE "a" DROP CONSTRAINT "six";]])
        t.assert_equals(ret.row_count, 1)
        t.assert_equals(s.foreign_key, nil)
        t.assert_equals(s.constraint, nil)
        t.assert_equals(s:format()[1].constraint, nil)
        t.assert_equals(s:format()[2].foreign_key, nil)

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
        local t = require('luatest')

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
