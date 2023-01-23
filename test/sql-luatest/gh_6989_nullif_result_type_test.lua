local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'test_assertion_in_modulo'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_assertion_in_modulo = function()
    g.server:exec(function()
        local sql = "SELECT nullif(1, '2');"
        local res = "integer"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = "SELECT nullif('1', '1');"
        res = "string"
        t.assert_equals(box.execute(sql).metadata[1].type, res)

        sql = "SELECT nullif([1], '2');"
        res = "Failed to execute SQL statement: wrong arguments for function "..
              "NULLIF()"
        local _, err = box.execute(sql)
        t.assert_equals(err.message, res)

        sql = "SELECT nullif(1, [2]);"
        res = "Type mismatch: can not convert array([2]) to scalar"
        _, err = box.execute(sql)
        t.assert_equals(err.message, res)
    end)
end
