local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-6990'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_case_operation_type = function()
    g.server:exec(function()
        local sql = [[SELECT CASE 1 WHEN 1 THEN NULL ELSE NULL END;]]
        local res = "any"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN 1 ELSE ? END;]]
        res = "any"
        t.assert_equals(box.execute(sql, {1}).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN [1] ELSE [2, 2] END;]]
        res = "array"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN 1 ELSE {1 : 1} END;]]
        res = "any"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN 1 ELSE {1 : 1} END;]]
        res = "any"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN 1 ELSE 'asd' END;]]
        res = "scalar"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN 1 ELSE CAST(1 AS NUMBER) END;]]
        res = "number"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN -1 ELSE CAST(1 AS UNSIGNED) END;]]
        res = "integer"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN -1 ELSE 1.5e0 END;]]
        res = "double"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT CASE 1 WHEN 1 THEN -1 WHEN 2 THEN 1.5 ELSE 2e0 END;]]
        res = "decimal"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = [[SELECT TYPEOF(CASE 1 WHEN 1 THEN 1 ELSE {1 : 1} END);]]
        res = "any"
        t.assert_equals(box.execute(sql).rows[1][1], res)

        sql = [[SELECT TYPEOF(CASE 1 WHEN 1 THEN 1 ELSE 'asd' END);]]
        res = "scalar"
        t.assert_equals(box.execute(sql).rows[1][1], res)

        sql = [[SELECT TYPEOF(CASE 1 WHEN 1 THEN -1 ELSE 1.5e0 END);]]
        res = "double"
        t.assert_equals(box.execute(sql).rows[1][1], res)
    end)
end
