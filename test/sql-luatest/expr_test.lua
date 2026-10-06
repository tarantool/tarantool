local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

--
-- Make sure that the text of an expression is taken exactly as written,
-- without the surrounding spaces and comments.
--
g.test_expr_text = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_full_metadata" = true;]])
        box.execute([[CREATE TABLE t (i INT PRIMARY KEY);]])
        local res = box.execute([[SELECT DISTINCT (1) + 2 /* c */ ,
                                  1 + (2) AS x, ( (1 + 2) ) * 3, t.i--c
                                  FROM SEQSCAN t;]])
        local spans = {}
        for _, column in ipairs(res.metadata) do
            table.insert(spans, column.span)
        end
        t.assert_equals(spans, {'(1) + 2', '1 + (2)', '( (1 + 2) ) * 3',
                                't.i'})
        box.execute([[SET SESSION "sql_full_metadata" = false;]])
        box.execute([[DROP TABLE t;]])

        box.execute([[CREATE TABLE t (i INT PRIMARY KEY,
                      a INT DEFAULT ( 1 + 2 ) CHECK (  a > 0 /* c */ ),
                      s STRING DEFAULT ( 'a' ) COLLATE "binary",
                      CONSTRAINT ck CHECK ( (a) + i > 0 ));]])
        local func = box.space._func.index.name
        t.assert_equals(func:get('default_t_a').body, '( 1 + 2 )')
        t.assert_equals(func:get('check_t_ck_unnamed_t_a_1').body, 'a > 0')
        t.assert_equals(func:get('check_t_ck').body, '(a) + i > 0')
        t.assert_equals(box.space.t:format()[3].default, 'a')
        box.execute([[DROP TABLE t;]])
    end)
end

--
-- gh-13205: make sure that COLLATE contributes to the expression tree
-- height.
--
g.test_collate_counts_towards_height = function()
    g.server:exec(function()
        local expr = "'a'" .. string.rep(" || 'a'", 198)

        local _, err = box.execute('SELECT (' .. expr .. " || 'a');")
        t.assert_equals(err, nil)

        _, err = box.execute('SELECT (' .. expr .. ') COLLATE binary;')
        t.assert_equals(err, nil)

        local exp_err = 'Number of nodes in expression tree 201 exceeds ' ..
                        'the limit (200)'
        _, err = box.execute('SELECT (' .. expr .. " || 'a') COLLATE binary;")
        t.assert_equals(err.message, exp_err)

        expr = "'a'" .. string.rep(' COLLATE binary', 199)
        _, err = box.execute('SELECT ' .. expr .. ';')
        t.assert_equals(err, nil)

        _, err = box.execute('SELECT ' .. expr .. ' COLLATE binary;')
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- gh-13205: make sure that the height of a subquery is counted towards
-- the height of the expression containing it only once.
--
g.test_subquery_height_counted_once = function()
    g.server:exec(function()
        local expr = '1' .. string.rep(' + 1', 198)

        local _, err = box.execute('SELECT (SELECT ' .. expr .. ');')
        t.assert_equals(err, nil)

        _, err = box.execute('SELECT 1 IN (SELECT ' .. expr .. ');')
        t.assert_equals(err, nil)

        _, err = box.execute('SELECT (SELECT (SELECT ' .. expr .. '));')
        local exp_err = 'Number of nodes in expression tree 201 exceeds ' ..
                        'the limit (200)'
        t.assert_equals(err.message, exp_err)

        _, err = box.execute('SELECT (SELECT ' .. expr .. ' + 1);')
        t.assert_equals(err.message, exp_err)
    end)
end

--
-- gh-13205: make sure that a too deep expression tree is rejected instead
-- of overflowing the stack.
--
g.test_very_deep_expr = function()
    g.server:exec(function()
        local n = 100000
        local function exp_err(height)
            return ('Number of nodes in expression tree %d exceeds ' ..
                    'the limit (200)'):format(height)
        end
        local concat = "'a'" .. string.rep(" || 'a'", n)
        local collate = "'a'" .. string.rep(' COLLATE binary', n)
        local cases = {
            {'SELECT ' .. concat .. ';', n + 1},
            {'SELECT ' .. collate .. ';', n + 1},
            {'SELECT 1' .. string.rep(' IN ()', n) .. ';', n + 1},
            {'SELECT false' .. string.rep(' AND false', n) .. ';', n + 1},
            {'SELECT 1 WHERE ' .. concat .. ' = 1;', n + 2},
            {'SELECT (SELECT ' .. concat .. ');', n + 2},
            {'VALUES (1), (' .. concat .. ');', n + 1},
            {'CREATE TABLE t (i INT PRIMARY KEY, s STRING DEFAULT (' ..
             concat .. '));', n + 1},
            {'CREATE TABLE t (i INT PRIMARY KEY, s STRING CHECK (s = ' ..
             concat .. '));', n + 2},
            {'CREATE VIEW v AS SELECT ' .. concat .. ';', n + 1},
        }
        for _, case in pairs(cases) do
            local _, err = box.execute(case[1])
            t.assert_equals(err.message, exp_err(case[2]), case[1]:sub(1, 40))
        end

        box.execute('CREATE TABLE t (i INT PRIMARY KEY, s STRING);')
        local _, err = box.execute('CREATE TRIGGER tr AFTER INSERT ON t ' ..
                                   'FOR EACH ROW BEGIN SELECT ' .. concat ..
                                   '; END;')
        t.assert_equals(err.message, exp_err(n + 1))
        box.execute('DROP TABLE t;')
    end)
end
