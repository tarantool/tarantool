local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
    end)
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure default is not dropped after ADD COLUMN.
g.test_default_after_add_column = function()
    g.server:exec(function()
        local body = 'function(a) return a + 123 end'
        local func_def = {is_deterministic = true, body = body}
        box.schema.func.create('F1', func_def)
        local format = {{'I', 'integer'}, {'A', 'integer', default = 321,
                                           default_func = 'F1'}}
        local s = box.schema.space.create('A', {format = format})
        local func = box.func.F1
        t.assert_equals(s:format()[2].default, 321)
        t.assert_equals(s:format()[2].default_func, func.id)
        box.execute([[ALTER TABLE A ADD COLUMN B INTEGER;]])
        t.assert_equals(s:format()[2].default, 321)
        t.assert_equals(s:format()[2].default_func, func.id)
        s:drop()
        func:drop()
    end)
end

-- Make sure default is not supported by SHOW CREATE TABLE.
g.test_default_in_show_create_table = function()
    g.server:exec(function()
        local body = 'function(a) return a + 123 end'
        local func_def = {is_deterministic = true, body = body}
        box.schema.func.create('F1', func_def)
        local format = {{'I', 'integer'}, {'A', 'integer', default = 321,
                                           default_func = 'F1'}}
        local s = box.schema.space.create('A', {format = format})
        s:create_index('ii')
        local func = box.func.F1
        t.assert_equals(s:format()[2].default, 321)
        t.assert_equals(s:format()[2].default_func, func.id)
        local rows = box.execute([[SHOW CREATE TABLE A;]]).rows
        local exp = "Problem with field 'A': BOX default values are "..
                    "unsupported."
        t.assert_equals(rows[1][2][1], exp)
        s:drop()
        func:drop()
    end)
end

-- Make sure SQL_EXPR functions works as default value.
g.test_sql_expr_default_func = function()
    g.server:exec(function()
        -- No default.
        local format = {{'a', 'integer'}, {'b', 'integer', is_nullable = true}}
        local s = box.schema.space.create('t', {format = format})
        local _ = s:create_index('ii')
        s:insert({1})
        local exp = {{1}}
        t.assert_equals(s:select(), exp)
        local func_def = {is_deterministic = true, language = 'SQL_EXPR'}

        -- Function with zero arguments.
        func_def.body = '123 + 456 * 2'
        box.schema.func.create('DEF1', func_def)
        format[2].default_func = 'DEF1'
        s:format(format)
        s:insert({2})
        exp[2] = {2, 1035}
        t.assert_equals(s:select(), exp)

        -- Function with one argument.
        func_def.body = '11 * a'
        box.schema.func.create('DEF2', func_def)
        format[2].default = 123
        format[2].default_func = 'DEF2'
        s:format(format)
        s:insert({3})
        exp[3] = {3, 11 * 123}
        t.assert_equals(s:select(), exp)

        -- Function with more than one argument.
        func_def.body = 'a * b * c'
        box.schema.func.create('DEF3', func_def)
        format[2].default_func = 'DEF3'
        s:format(format)
        s:insert({4})
        exp[4] = {4, 123 * 123 * 123}
        t.assert_equals(s:select(), exp)

        s:drop()
        box.func.DEF3:drop()
        box.func.DEF2:drop()
        box.func.DEF1:drop()
    end)
end

-- Make sure that numeric values with sign are encoded as the literal defaults.
g.test_numbers_with_signs = function()
    g.server:exec(function()
        local decimal = require('decimal')
        local sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT -1);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{-1}})
        t.assert_equals(box.space.t:format()[2].default, -1)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT (-1));]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{-1}})
        t.assert_equals(box.space.t:format()[2].default, -1)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT (+1));]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{1}})
        t.assert_equals(box.space.t:format()[2].default, 1)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT +1);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{1}})
        t.assert_equals(box.space.t:format()[2].default, 1)
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT -1.1e0);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{-1.1}})
        t.assert_equals(box.space.t:format()[2].default, -1.1)
        t.assert_equals(type(box.space.t:format()[2].default), 'number')
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT (-1.1e0));]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{-1.1}})
        t.assert_equals(box.space.t:format()[2].default, -1.1)
        t.assert_equals(type(box.space.t:format()[2].default), 'number')
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT (+1.1e0));]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{1.1}})
        t.assert_equals(box.space.t:format()[2].default, 1.1)
        t.assert_equals(type(box.space.t:format()[2].default), 'number')
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT +1.1e0);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{1.1}})
        t.assert_equals(box.space.t:format()[2].default, 1.1)
        t.assert_equals(type(box.space.t:format()[2].default), 'number')
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT -1.1);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{-1.1}})
        t.assert_equals(box.space.t:format()[2].default, -1.1)
        t.assert(decimal.is_decimal(box.space.t:format()[2].default))
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT (-1.1));]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{-1.1}})
        t.assert_equals(box.space.t:format()[2].default, -1.1)
        t.assert(decimal.is_decimal(box.space.t:format()[2].default))
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT (+1.1));]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{1.1}})
        t.assert_equals(box.space.t:format()[2].default, 1.1)
        t.assert(decimal.is_decimal(box.space.t:format()[2].default))
        box.execute([[DROP TABLE t;]])

        sql = [[CREATE TABLE t(i INT PRIMARY KEY, a ANY DEFAULT +1.1);]]
        t.assert_equals(box.execute(sql), {row_count = 1})
        box.execute([[INSERT INTO t(i) VALUES (1);]])
        t.assert_equals(box.execute([[SELECT a FROM t;]]).rows, {{1.1}})
        t.assert_equals(box.space.t:format()[2].default, 1.1)
        t.assert(decimal.is_decimal(box.space.t:format()[2].default))
        box.execute([[DROP TABLE t;]])
    end)
end

g.test_new_defaults = function()
    g.server:exec(function()
        -- Make sure a literal works correctly as a default value.
        local sql = [[CREATE TABLE T(I INT PRIMARY KEY, A ANY DEFAULT 123,
                      B STRING DEFAULT('something'));]]
        box.execute(sql)
        t.assert_equals(box.space.T:format()[2].default, 123)
        t.assert_equals(box.space.T:format()[3].default, 'something')
        box.space.T:drop()
    end)
end

g.test_new_func_defaults = function()
    g.server:exec(function()
        -- Make sure an expression works correctly as a default value.
        local sql = [[CREATE TABLE T(I INT PRIMARY KEY, A ANY DEFAULT(9 * 7));]]
        box.execute(sql)
        local s = box.space.T
        local func = box.func.default_T_A
        t.assert_equals(func.body, '(9 * 7)')
        t.assert_equals(s:format()[2].default_func, func.id)
        s:insert({1})
        t.assert_equals(s:select(), {{1, 63}})
        box.execute([[INSERT INTO T VALUES (2, NULL);]])
        t.assert_equals(s:select(), {{1, 63}, {2, 63}})
        s:drop()
        func:drop()
    end)
end

--
-- Make sure that function expressions are processed correctly when specified
-- in the DEFAULT clause without parentheses.
--
g.test_func_defaults_without_parentheses = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE T(I INT PRIMARY KEY,
                      A ANY DEFAULT 1 + 1, B ANY DEFAULT (1 + 1));]]
        box.execute(sql)
        local func_id_2 = box.space.T:format()[2].default_func
        t.assert_equals(box.space._func:get{func_id_2}.body, '1 + 1')
        local func_id_3 = box.space.T:format()[3].default_func
        t.assert_equals(box.space._func:get{func_id_3}.body, '(1 + 1)')
        box.space.T:drop()
        box.schema.func.drop(func_id_2)
        box.schema.func.drop(func_id_3)
    end)
end

--
-- Make sure that the NULL, NOT NULL, and COLLATE column properties do not cause
-- an error when using the DEFAULT clause with these keywords.
--
g.test_func_defaults_with_null_and_collate = function()
    g.server:exec(function()
        local sql = [[CREATE TABLE T(I INT PRIMARY KEY,
                      A ANY DEFAULT 'a' COLLATE "unicode" COLLATE "unicode_ci",
                      B ANY DEFAULT NULL IS NULL NULL,
                      C ANY DEFAULT NULL NULL,
                      D ANY DEFAULT NULL NOT NULL);]]
        box.execute(sql)

        local exp = box.space._collation.index.name:get{'unicode_ci'}.id;
        t.assert_equals(box.space.T:format()[2].collation, exp)
        t.assert_equals(box.space.T:format()[2].default, 'a')

        local func_id = box.space.T:format()[3].default_func
        t.assert_equals(box.space._func:get{func_id}.body, 'NULL IS NULL')
        t.assert_equals(box.space.T:format()[3].is_nullable, true)

        t.assert_equals(box.space.T:format()[4].is_nullable, true)

        t.assert_equals(box.space.T:format()[5].is_nullable, false)

        local exp_err = "Failed to execute SQL statement: " ..
                        "NOT NULL constraint failed: T.D"
        local _, err = box.execute([[INSERT INTO T(I) VALUES(1);]])
        t.assert_equals(err.message, exp_err)

        box.space.T:drop()
        box.schema.func.drop(func_id)
    end)
end
