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
-- Numerically equal decimal literals that differ in scale (e.g. 1.0 and
-- 1.00) must not be treated as the same expression. Otherwise constant
-- factoring reuses the register of the first literal for the second one,
-- and the second result silently loses its scale.
--
g.test_decimal_scale_is_not_shared = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a DECIMAL);]])
        box.execute([[INSERT INTO t VALUES(1, 10);]])

        local res = box.execute([[SELECT 1.0 + a, 1.00 + a FROM t;]])
        t.assert_equals(tostring(res.rows[1][1]), '11.0')
        t.assert_equals(tostring(res.rows[1][2]), '11.00')

        -- The scale of a subexpression must not depend on a sibling.
        res = box.execute([[SELECT 1.00 + a, 1.0 + a FROM t;]])
        t.assert_equals(tostring(res.rows[1][1]), '11.00')
        t.assert_equals(tostring(res.rows[1][2]), '11.0')

        -- The change is observable, not only cosmetic: a redundant
        -- always-true predicate must not drop the row.
        res = box.execute([[SELECT i FROM t WHERE 1.0 + a > 0 AND
                            CAST(1.00 + a AS STRING) = '11.00';]])
        t.assert_equals(res.rows, {{1}})

        box.execute([[DROP TABLE t;]])
    end)
end

--
-- A unary minus applied to a decimal literal is folded into a single signed
-- decimal literal at parse time. The negated value and its scale must be
-- preserved. A unary plus is folded away as a no-op.
--
g.test_uminus_decimal_literal = function()
    g.server:exec(function()
        local res = box.execute([[SELECT -1.5, -0.0, - -2.50, +2.50, +-1.5;]])
        t.assert_equals(tostring(res.rows[1][1]), '-1.5')
        t.assert_equals(tostring(res.rows[1][2]), '0.0')
        t.assert_equals(tostring(res.rows[1][3]), '2.50')
        t.assert_equals(tostring(res.rows[1][4]), '2.50')
        t.assert_equals(tostring(res.rows[1][5]), '-1.5')
        res = box.execute([[SELECT typeof(-1.5), typeof(+1.5);]])
        t.assert_equals(res.rows, {{'decimal', 'decimal'}})
    end)
end
