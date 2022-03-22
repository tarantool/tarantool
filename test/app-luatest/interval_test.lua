local t = require('luatest')
local g = t.group()
local ffi = require('ffi')
local msgpack = require('msgpack')
local msgpackffi = require('msgpackffi')

-- Interval is 1 year, 127 days and 15 minutes
local val = msgpack.decode('\xc7\x07\x06\x03\x00\x01\x03\x7f\x05\x0f')
local valffi = msgpackffi.decode('\xc7\x07\x06\x03\x00\x01\x03\x7f\x05\x0f')

g.test_check_interval_cdata = function()
    t.assert_equals(type(val), 'cdata')
end

g.test_check_interval_fields = function()
    t.assert_equals(val.year, 1)
    t.assert_equals(val.month, 0)
    t.assert_equals(val.week, 0)
    t.assert_equals(val.day, 127)
    t.assert_equals(val.hour, 0)
    t.assert_equals(val.min, 15)
    t.assert_equals(val.sec, 0)
    t.assert_equals(val.nsec, 0)
    t.assert_equals(val.adjust, 0)
end

g.test_check_equal_vals = function()
    t.assert_equals(val.year, valffi.year)
    t.assert_equals(val.month, valffi.month)
    t.assert_equals(val.week, valffi.week)
    t.assert_equals(val.day, valffi.day)
    t.assert_equals(val.hour, valffi.hour)
    t.assert_equals(val.min, valffi.min)
    t.assert_equals(val.sec, valffi.sec)
    t.assert_equals(val.nsec, valffi.nsec)
    t.assert_equals(val.adjust, valffi.adjust)
end

g.test_check_interval_ffi_typeof = function()
    t.assert_equals(tostring(ffi.typeof(val)), 'ctype<struct interval>')
    t.assert_equals(tostring(ffi.typeof(valffi)), 'ctype<struct interval>')
end
