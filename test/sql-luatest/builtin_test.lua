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
-- ghs-160: The built-in REPLACE function threw a segmentation fault if
-- the length of the first argument was less than the length of the second.
--
g.test_segfault_in_replace = function(cg)
    cg.server:exec(function()
        local res = box.execute([[SELECT REPLACE('', 'ab', 'xy');]])
        t.assert_equals(res.rows, {{''}})
        res = box.execute([[SELECT REPLACE('a', 'ab', 'xy');]])
        t.assert_equals(res.rows, {{'a'}})
    end)
end

--
-- ghs-161: The PRINTF built-in function could have an integer overflow.
--
g.test_integer_overflow_in_printf = function(cg)
    cg.server:exec(function()
        local exp = "Failed to execute SQL statement: string or blob too big"
        local sql = [[SELECT LENGTH(PRINTF('%2200000000s', 'a'));]]
        local _, err = box.execute(sql)
        t.assert_equals(err.message, exp)
    end)
end
