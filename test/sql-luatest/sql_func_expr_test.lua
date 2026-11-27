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
        local format = {{'a', 'integer'}, {'b', 'integer'}}
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
        format = {{name = 'a', type = 'integer', constraint = 'abc'}}
        s = box.schema.space.create('test', {format = format})
        s:create_index('i')
        t.assert_equals(s:insert{4}, {4})
        t.assert_error_msg_content_equals(
            "Check constraint 'abc' failed for field '1 (a)'",
            function() s:insert{1} end
        )
        box.space.test:drop()
        box.schema.func.drop('abc')

        def = {language = 'SQL_EXPR', is_deterministic = true,
               body = 'x + y > 0'}
        box.schema.func.create('abc', def)
        format = {{name = 'a', type = 'integer', constraint = 'abc'}}
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
        local func_create = box.schema.func.create
        local def = {language = 'SQL_EXPR', is_deterministic = true, body = ''}
        local exp_err = "Function definition cannot be empty"
        t.assert_error_msg_content_equals(exp_err, func_create, 'a1', def)

        def.body = ' '
        exp_err = "Syntax error at line 1 near ' '"
        t.assert_error_msg_content_equals(exp_err, func_create, 'a1', def)

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
        local format = {{'a', 'integer'}, {'b', 'integer'}}
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
        local format = {{'a', 'integer'}, {'b', 'integer', is_nullable = true}}
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
        local format = {{'a', 'integer'}, {'b', 'integer', is_nullable = true}}
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
        local format = {{'a', 'integer'}, {'b', 'integer'}}
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

--
-- Make sure that the body of a SQL expression function cannot be
-- parsed outside the function.
--
g.test_sql_expr_parsing = function()
    g.server:exec(function()
        local _, err = box.execute([[FUNCTION a * b > 10]])
        local exp_err = "Syntax error at line 1 near 'FUNCTION'"
        t.assert_equals(err.message, exp_err)

        _, err = box.execute([[a * b > 10]])
        exp_err = "Syntax error at line 1 near 'a'"
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- gh-13358: make sure that errors from the `sql_finish_coding()` function
-- and from functions called by it are not lost.
--
g.test_13358_errors_from_sql_finish_coding_are_not_lost = function (cg)
    cg.server:exec(function ()
        local exp_err = "COLLATE clause can't be used with non-string arguments"
        local sql = [[CREATE TABLE s (a ANY NULL,
                                      CHECK (a = 1 COLLATE "binary"),
                                      b UNSIGNED PRIMARY KEY);]]
        local _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        local f_err = {
            language = 'SQL_EXPR',
            is_deterministic = true,
            body = 'a = 1 COLLATE "binary"'
        }
        t.assert_error_msg_equals(exp_err, box.schema.func.create, 'f', f_err)

        exp_err = "row value misused"
        sql = [[CREATE TABLE s (a ANY NULL, CHECK (a = 1 OR (1, 2) IS NULL),
                                b INT PRIMARY KEY);]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "RAISE() may only be used within a trigger-program"
        sql = [[CREATE TABLE s
                (a ANY NULL, CHECK (a = 1 OR RAISE(IGNORE) IS NULL),
                 b INT PRIMARY KEY)]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Illegal mix of collations"
        sql = [[CREATE TABLE s
                (a ANY NULL, CHECK
                    (a = 1 OR 'x' COLLATE "unicode_ci" = 'y' COLLATE "binary"),
                 b INT PRIMARY KEY)]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Collation 'no_such_collation' does not exist"
        sql = [[CREATE TABLE s
                (a ANY NULL, CHECK
                    (a = 1 OR 'x' COLLATE "no_such_collation" = 'y'),
                 b INT PRIMARY KEY)]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "SQL expressions does not support reference to spaces"
        sql = [[CREATE TABLE s (a INT PRIMARY KEY, CHECK (s.a > 0));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "SQL expressions does not support subselects"
        sql = [[CREATE TABLE s (a INT PRIMARY KEY,
                                CHECK ((SELECT a ORDER BY SUM(a)) > 0));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Wrong number of arguments is passed to COALESCE(): "..
                  "expected at least 2, got 0"
        sql = [[CREATE TABLE s (id INTEGER PRIMARY KEY,
                                data INTEGER, CHECK (COALESCE() is NOT NULL));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Wrong number of arguments is passed to LIKELY(): "..
                  "expected 1, got 0"
        sql = [[CREATE TABLE s (a INT PRIMARY KEY DEFAULT (LIKELY()));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "SQL expressions does not support subselects"
        sql = [[CREATE TABLE s (a INT PRIMARY KEY, CHECK ((SELECT 1) > 0));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Hex literal 0xFFFFFFFFFFFFFFFFF length 17 "..
                  "exceeds the supported limit (16)"
        sql = [[CREATE TABLE s (id INTEGER PRIMARY KEY, data INTEGER CHECK
                                (data > 0xFFFFFFFFFFFFFFFFF));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Integer literal 9223372036854775808999 exceeds "..
                  "the supported range "..
                  "[-9223372036854775808, 18446744073709551615]"
        sql = [[CREATE TABLE s (id INTEGER PRIMARY KEY, data INTEGER,
                                CHECK (data > 9223372036854775808999));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Only integer, string and uuid can be keys in map"
        sql = [[CREATE TABLE s (a UNSIGNED PRIMARY KEY,
                                CHECK (a = 1 OR {1.5: 'x'} IS NOT NULL));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Selecting is only possible from map and array values"
        sql = [[CREATE TABLE s (id INT PRIMARY KEY,
                                a INTEGER, CHECK(id[1] > 0));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Selecting is only possible from map and array values"
        sql = [[CREATE TABLE s (id INT PRIMARY KEY, arr ARRAY, key_arr ARRAY,
                                CHECK(arr[key_arr] > 0));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Wrong number of arguments is passed to ABS(): "..
                  "expected 1, got 0"
        sql = "CREATE TABLE s (id INT PRIMARY KEY, a INTEGER DEFAULT (ABS()));"
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Failed to execute SQL statement: "..
                  "wrong arguments for function ABS()"
        sql = [[CREATE TABLE s (id INT PRIMARY KEY,
                                a INTEGER DEFAULT (ABS(true)));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Function 'no_such_func' does not exist"
        sql = [[CREATE TABLE t (id INT PRIMARY KEY,
                                a INTEGER DEFAULT (no_such_func(1)));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        box.schema.func.create('LUA_FUNC_1', {
            body = 'function() return 1 end',
            exports = {'LUA'}
        })

        exp_err = "function lua_func_1() is not available in SQL"
        sql = [[CREATE TABLE s (id INT PRIMARY KEY,
                                a INTEGER DEFAULT (lua_func_1(1)));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        box.schema.func.create('SQL_FUNC_1', {
            body = 'function(x) return x end',
            param_list = {'integer'},returns = 'integer',
            exports = {'LUA', 'SQL'}
        })

        exp_err = "Wrong number of arguments is passed to sql_func_1(): "..
                  "expected 1, got 0"
        sql = [[CREATE TABLE t (id INT PRIMARY KEY,
                                a INTEGER DEFAULT (sql_func_1()));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)

        exp_err = "Illegal mix of collations"
        sql = [[CREATE TABLE t (id INT PRIMARY KEY,
                                b BOOLEAN DEFAULT
                                ('a' COLLATE "binary" = 'a'
                                COLLATE "unicode"));]]
        _, err = box.execute(sql)
        t.assert_equals(tostring(err), exp_err)
        box.schema.func.drop('LUA_FUNC_1')
        box.schema.func.drop('SQL_FUNC_1')
    end)
end
