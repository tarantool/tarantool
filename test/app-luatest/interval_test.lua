local t = require('luatest')
local g = t.group()
local itv = require('datetime').interval

local val = itv.new({year = 1, day = 127, min = 15})

g.test_check_interval_totable = function()
    local tval = val:totable()
    t.assert_equals(type(tval), "table")
    local tres = {year = 1, month = 0, week = 0, day = 127, hour = 0, min = 15,
                  sec = 0, nsec = 0, adjust = "none"}
    t.assert_equals(tval, tres)
    -- Now INTERVAL values can be used in require('luatest').assert_equals().
    t.assert_equals(val, itv.new({year = 1, day = 127, min = 15}))
end
