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
    end)
end
