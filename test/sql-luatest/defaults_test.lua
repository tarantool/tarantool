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
