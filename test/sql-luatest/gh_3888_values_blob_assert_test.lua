local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- Make sure that tokens representing the literals of integer, numeric,
-- and binary types are distinct from tokens representing the names
-- for those types.
--
g.test_3888_values_blob_assert = function(cg)
    cg.server:exec(function()
        -- Check 'VALUES' against typedef keywords (should fail).
        local _, err = box.execute('VALUES(scalar);')
        t.assert_equals(err.message, "Syntax error at line 1 near 'scalar'")
        _, err = box.execute('VALUES(float);')
        t.assert_equals(err.message, "Syntax error at line 1 near 'float'")

        -- Check 'SELECT' against typedef keywords (should fail).
        _, err = box.execute('SELECT scalar;')
        t.assert_equals(err.message, "Syntax error at line 1 near 'scalar'")
        _, err = box.execute('SELECT float;')
        t.assert_equals(err.message, "Syntax error at line 1 near 'float'")

        -- Check 'VALUES' against ID (should fail).
        _, err = box.execute('VALUES(TheColumnName);')
        t.assert_equals(err.message, "Can't resolve field 'TheColumnName'")

        -- Check 'SELECT' against ID (should fail).
        _, err = box.execute('SELECT TheColumnName;')
        t.assert_equals(err.message, "Can't resolve field 'TheColumnName'")

        -- Check 'VALUES' well-formed expression  (returns value).
        local res = box.execute('VALUES(-0.5e-2);')
        t.assert_equals(res.rows, {{-0.005}})
        res = box.execute("VALUES(X'507265766564');")
        t.assert_equals(res.rows, {{"\x50\x72\x65\x76\x65\x64"}})

        -- Check 'SELECT' well-formed expression  (return value).
        res = box.execute('SELECT 3.14;')
        t.assert_equals(res.rows, {{3.14}})
        res = box.execute("SELECT X'4D6564766564';")
        t.assert_equals(res.rows, {{"\x4D\x65\x64\x76\x65\x64"}})
    end)
end
