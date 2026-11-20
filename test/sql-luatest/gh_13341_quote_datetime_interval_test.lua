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

-- Make sure QUOTE() returns the string representation of its DATETIME
-- argument.
g.test_quote_datetime = function()
    g.server:exec(function()
        local dt = require('datetime')
        local arg = dt.new({year = 2001, month = 2, day = 3, hour = 4})
        local res, err = box.execute([[SELECT QUOTE(?);]], {arg})
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, {{'2001-02-03T04:00:00Z'}})
    end)
end

-- Make sure QUOTE() returns the string representation of its INTERVAL
-- argument.
g.test_quote_interval = function()
    g.server:exec(function()
        local itv = require('datetime').interval
        local arg = itv.new({year = 1, month = 2})
        local res, err = box.execute([[SELECT QUOTE(?);]], {arg})
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, {{'+1 years, 2 months'}})
    end)
end

-- Make sure the result of QUOTE() does not depend on the value it returned
-- for the previous row.
g.test_quote_after_another_row = function()
    g.server:exec(function()
        local dt = require('datetime')
        local s = box.schema.space.create('t', {
            format = {{'i', 'integer'}, {'a', 'any'}},
        })
        s:create_index('pk')
        s:insert({1, 86400.5})
        s:insert({2, dt.new({year = 2001, month = 2, day = 3, hour = 4})})
        s:insert({3, dt.interval.new({year = 1, month = 2})})
        box.execute([[SET SESSION "sql_seq_scan" = true;]])
        local res, err = box.execute([[SELECT QUOTE(a) FROM t;]])
        t.assert_equals(err, nil)
        t.assert_equals(res.rows, {
            {86400.5},
            {'2001-02-03T04:00:00Z'},
            {'+1 years, 2 months'},
        })
        s:drop()
    end)
end
