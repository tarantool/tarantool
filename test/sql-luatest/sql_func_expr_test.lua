local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'sql_func_expr'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

-- Make sure SQL_EXPR function works correctly as a tuple or a field constraint.
g.test_sql_func_expr_1 = function()
    g.server:exec(function()
        local def = {language = 'SQL_EXPR', is_deterministic = true,
                     body = 'a * b > 10'}
        box.schema.func.create('abc', def)
        local format = {{'A', 'integer'}, {'B', 'integer'}}
        local s = box.schema.space.create('test', {format = format})
        s:create_index('i')
        s:alter{constraint='abc'}
        t.assert_equals(s:insert{3, 4}, {3, 4})
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for a tuple",
            function() s:insert{1, 2} end
        )
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for a tuple",
            function() s:insert{true, 2} end
        )
        box.space.test:drop()
        box.schema.func.drop('abc')

        def = {language = 'SQL_EXPR', is_deterministic = true,
               body = 'x * x % 10 == 6'}
        box.schema.func.create('abc', def)
        format = {{name = 'A', type = 'integer', constraint = 'abc'}}
        s = box.schema.space.create('test', {format = format})
        s:create_index('i')
        t.assert_equals(s:insert{4}, {4})
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for field '1 (A)'",
            function() s:insert{1} end
        )
        box.space.test:drop()
        box.schema.func.drop('abc')

        def = {language = 'SQL_EXPR', is_deterministic = true,
               body = 'x + y > 0'}
        box.schema.func.create('abc', def)
        format = {{name = 'A', type = 'integer', constraint = 'abc'}}
        t.assert_error_msg_content_equals(
            "Failed to create constraint 'abc' in space 'test': Number of "..
            "arguments in a SQL field constraint function is greater than one",
            function() box.schema.space.create('test', {format = format}) end
        )
        box.schema.func.drop('abc')
    end)
end

-- Make sure SQL_EXPRESSION function parsed properly.
g.test_sql_func_expr_2 = function()
    g.server:exec(function()
        local def = {language = 'SQL_EXPR', is_deterministic = true, body = ''}
        t.assert_error_msg_content_equals(
            "Syntax error at line 1 near ' '",
            function() box.schema.func.create('a1', def) end
        )

        def.body = ' '
        t.assert_error_msg_content_equals(
            "Syntax error at line 1 near '  '",
            function() box.schema.func.create('a1', def) end
        )

        def.body = '1, 1 '
        t.assert_error_msg_content_equals(
            "Syntax error at line 1 near ','",
            function() box.schema.func.create('a1', def) end
        )

        def.body = 'a + (SELECT "id" AS a FROM "_space" LIMIT 1);'
        t.assert_error_msg_content_equals(
            "SQL expressions does not support subselects",
            function() box.schema.func.create('a1', def) end
        )
    end)
end

-- Make sure SQL EXPR recovers properly after restart.
g.test_sql_func_expr_3 = function()
    g.server:exec(function()
        local def = {language = 'SQL_EXPR', is_deterministic = true,
                     body = 'a * b > 10'}
        box.schema.func.create('abc', def)
        local format = {{'A', 'integer'}, {'B', 'integer'}}
        local s = box.schema.create_space('test', {format = format})
        s:create_index('i')
        s:alter{constraint='abc'}
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for a tuple",
            function() s:insert{1, 1} end
        )
    end)
    g.server:restart()
    g.server:exec(function()
        t.assert_equals(box.func.abc.language, 'SQL_EXPR')
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for a tuple",
            function() box.space.test:insert{2, 2} end
        )
        t.assert_equals(box.space.test:insert{7, 7}, {7, 7})
        box.space.test:drop()
        box.schema.func.drop('abc')
    end)
end

-- Make sure CHECK constraint works as intended when last field is nullable.
g.test_sql_func_expr_4 = function()
    g.server:exec(function()
        local def = {language = 'SQL_EXPR', is_deterministic = true,
                     body = 'a * b > 10'}
        box.schema.func.create('abc', def)
        local format = {{'A', 'integer'}, {'B', 'integer', is_nullable = true}}
        local s = box.schema.space.create('test', {format = format})
        s:create_index('i')
        s:alter{constraint='abc'}
        t.assert_equals(s:insert{3, 4}, {3, 4})
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for a tuple",
            function() s:insert{1, 2} end
        )
        box.space.test:drop()
        box.schema.func.drop('abc')
    end)
end

-- Make sure CHECK constraint works as intended when last field is nullable.
g.test_sql_func_expr_4 = function()
    g.server:exec(function()
        local def = {language = 'SQL_EXPR', is_deterministic = true,
                     body = 'a * b > 10'}
        box.schema.func.create('abc', def)
        local format = {{'A', 'integer'}, {'B', 'integer', is_nullable = true}}
        local s = box.schema.space.create('test', {format = format})
        s:create_index('i')
        s:alter{constraint='abc'}
        t.assert_equals(s:insert{3, 4}, {3, 4})
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for a tuple",
            function() s:insert{1, 2} end
        )
        box.space.test:drop()
        box.schema.func.drop('abc')
    end)
end

-- Make sure SQL EXPR do not expire.
g.test_sql_func_expr_5 = function()
    g.server:exec(function()
        local def = {language = 'SQL_EXPR', is_deterministic = true,
                     body = 'a * b > 10'}
        box.schema.func.create('abc', def)
        local format = {{'A', 'integer'}, {'B', 'integer'}}
        local s = box.schema.space.create('test', {format = format})
        s:create_index('i')
        s:alter{constraint='abc'}
        t.assert_equals(s:insert{3, 4}, {3, 4})
        box.execute([[CREATE INDEX i1 ON "test"(a);]])
        t.assert_equals(s:insert{4, 5}, {4, 5})
        box.space.test:drop()
        box.schema.func.drop('abc')
    end)
end
