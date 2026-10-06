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
