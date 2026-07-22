local server = require('luatest.server')
local t = require('luatest')

local g = t.group('parser')

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Make sure that syntax errors occur before semantic errors.
g.test_syntax_errors = function(cg)
    cg.server:exec(function()
        local _, err = box.execute([[SELECT i FROM t 1;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '1'")

        _, err = box.execute([[DROP TABLE t 2;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '2'")

        _, err = box.execute([[DROP VIEW v 3;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '3'")

        _, err = box.execute([[DROP TRIGGER t 4;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '4'")

        _, err = box.execute([[DROP INDEX I ON t 5;]])
        t.assert_equals(err.message, "Syntax error at line 1 near '5'")
    end)
end
