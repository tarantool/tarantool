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

--
-- gh-13683: Introduce CURRENT_TIMESTAMP [(p)]
--
g.test_current_timestamp_types = function(cg)
    cg.server:exec(function()
        local res
        
        res = box.execute([[SELECT typeof(CURRENT_TIMESTAMP);]])
        t.assert_equals(res.rows[1][1], 'datetime')
        
        res = box.execute([[SELECT typeof(CURRENT_TIMESTAMP(3));]])
        t.assert_equals(res.rows[1][1], 'datetime')
        
        res = box.execute([[SELECT typeof(CURRENT_TIMESTAMP(0));]])
        t.assert_equals(res.rows[1][1], 'datetime')
    end)
end

g.test_current_timestamp_errors = function(cg)
    cg.server:exec(function()
        local _, err
        
        _, err = box.execute([[SELECT CURRENT_TIMESTAMP();]])
        t.assert_equals(err.message, "Syntax error at line 1 near ')'")
        
        _, err = box.execute([[SELECT CURRENT_TIMESTAMP(?);]])
        t.assert_equals(err.message, "Syntax error at line 1 near '?'")
        
        _, err = box.execute([[SELECT CURRENT_TIMESTAMP(1+1);]])
        t.assert_equals(err.message, "Syntax error at line 1 near '+'")
        
        _, err = box.execute([[SELECT CURRENT_TIMESTAMP(10);]])
        t.assert_equals(err.message, "Failed to execute SQL statement: " ..
                    "CURRENT_TIMESTAMP precision must be between 0 and 9")
    end)
end