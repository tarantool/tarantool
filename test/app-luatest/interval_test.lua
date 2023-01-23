local t = require('luatest')
local msgpack = require('msgpack')
local msgpackffi = require('msgpackffi')
local itv = require('datetime').interval

local g = t.group()

-- Interval is 1 year, 127 days and 15 minutes
local bin = '\xc7\x09\x06\x04\x00\x01\x03\x7f\x05\x0f\x08\x01'
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

g.test_check_interval_decode = function()
    t.assert_equals(msgpack.decode(bin), val)
end

g.test_check_interval_encode = function()
    t.assert_equals(bin, msgpack.encode(val))
end

g.test_check_interval_decode_ffi = function()
    t.assert_equals(msgpackffi.decode(bin), val)
end

g.test_check_interval_encode_ffi = function()
    t.assert_equals(bin, msgpackffi.encode(val))
end
