local server = require('luatest.server')
local t = require('luatest')

local g = t.group("values", {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function(engine)
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local sql = [[SET SESSION "sql_default_engine" = '%s']]
        box.execute(sql:format(engine))
    end, {cg.params.engine})
end)

g.after_all(function(cg)
    cg.server:drop()
end)

--
-- Make sure that tokens representing values of integer, float,
-- and blob constants are different from tokens representing
-- keywords of the same names.
--
g.test_3888_values_blob_assert = function(cg)
    cg.server:exec(function()
        -- check 'VALUES' against typedef keywords (should fail)
        local _, err = box.execute('VALUES(scalar)')
        t.assert_equals(err.message, "Syntax error at line 1 near 'scalar'")
        _, err = box.execute('VALUES(float)')
        t.assert_equals(err.message, "Syntax error at line 1 near 'float'")

        -- check 'SELECT' against typedef keywords (should fail)
        _, err = box.execute('SELECT scalar')
        t.assert_equals(err.message, "Syntax error at line 1 near 'scalar'")
        _, err = box.execute('SELECT float')
        t.assert_equals(err.message, "Syntax error at line 1 near 'float'")

        -- check 'VALUES' against ID (should fail)
        _, err = box.execute('VALUES(TheColumnName)')
        t.assert_equals(err.message, "Can't resolve field 'TheColumnName'")

        -- check 'SELECT' against ID (should fail)
        _, err = box.execute('SELECT TheColumnName')
        t.assert_equals(err.message, "Can't resolve field 'TheColumnName'")

        -- check 'VALUES' well-formed expression  (returns value)
        local res = box.execute('VALUES(-0.5e-2)')
        t.assert_equals(res.rows, {{-0.005}})
        res = box.execute("SELECT X'507265766564'")
        t.assert_equals(res.rows, {{"\x50\x72\x65\x76\x65\x64"}})

        -- check 'SELECT' well-formed expression  (return value)
        res = box.execute('SELECT 3.14')
        t.assert_equals(res.rows, {{3.14}})
        res = box.execute("SELECT X'4D6564766564'")
        t.assert_equals(res.rows, {{"\x4D\x65\x64\x76\x65\x64"}})
    end)
end
