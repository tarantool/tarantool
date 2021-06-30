#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('errno')
local date = require('datetime')
local ffi = require('ffi')

test:plan(12)

-- minimum supported date - -5879610-06-22
local MIN_DATE_YEAR = -5879610
local MIN_DATE_MONTH = 6
local MIN_DATE_DAY = 22
-- maximum supported date - 5879611-07-11
local MAX_DATE_YEAR = 5879611
local MAX_DATE_MONTH = 7
local MAX_DATE_DAY = 11

local incompat_types = 'incompatible types for comparison'
local only_integer_ts = 'only integer values allowed in timestamp'..
                        ' if nsec, usec, or msecs provided'
local only_one_of = 'only one of nsec, usec or msecs may be defined'..
                    ' simultaneously'
local timestamp_and_ymd = 'timestamp is not allowed if year/month/day provided'
local timestamp_and_hms = 'timestamp is not allowed if hour/min/sec provided'
local str_or_num_exp = 'tzoffset: string or number expected, but received'
local numeric_exp = 'numeric value expected, but received '

-- various error message generators
local function exp_datetime(name, value)
    return ("%s: expected datetime, but received %s"):format(name, type(value))
end

local function nyi_error(msg)
    return ("Not yet implemented : '%s'"):format(msg)
end

local function table_expected(msg, value)
    return ("%s: expected table, but received %s"):
            format(msg, type(value))
end

local function expected_str(msg, value)
    return ("%s: expected string, but received %s"):format(msg, type(value))
end

local function invalid_days_in_mon(d, M, y)
    return ('invalid number of days %d in month %d for %d'):format(d, M, y)
end

local function range_check_error(name, value, range)
    return ('value %s of %s is out of allowed range [%d, %d]'):
              format(value, name, range[1], range[2])
end

local function range_check_3_error(name, value, range)
    return ('value %d of %s is out of allowed range [%d, %d..%d]'):
            format(value, name, range[1], range[2], range[3])
end

local function less_than_min(y, M, d)
    return ('date %d-%02d-%02d is less than minimum allowed %d-%02d-%02d'):
            format(y, M, d, MIN_DATE_YEAR, MIN_DATE_MONTH, MIN_DATE_DAY)
end

local function greater_than_max(y, M, d)
    return ('date %d-%02d-%02d is greater than maximum allowed %d-%02d-%02d'):
            format(y, M, d, MAX_DATE_YEAR, MAX_DATE_MONTH, MAX_DATE_DAY)
end

local function invalid_tz_fmt_error(val)
    return ('invalid time-zone format %s'):format(val)
end

-- utility functions to gracefully handle pcall errors
local function assert_raises(test, error_msg, func, ...)
    local ok, err = pcall(func, ...)
    local err_tail = err:gsub("^.+:%d+: ", "")
    return test:is(not ok and err_tail, error_msg,
                   ('"%s" received, "%s" expected'):format(err_tail, error_msg))
end

local function assert_raises_like(test, error_msg, func, ...)
    local ok, err = pcall(func, ...)
    local err_tail = err:gsub("^.+:%d+: ", "")
    return test:like(not ok and err_tail, error_msg,
                   ('"%s" received, "%s" expected'):format(err_tail, error_msg))
end

test:test("Datetime API checks", function(test)
    test:plan(12)
    local ts = date.new()
    local local_format = ts.format
    local local_totable = ts.totable
    local local_set = ts.set

    test:is(local_format(ts), "1970-01-01T00:00:00Z", "correct :format")
    local table_expected = {
        sec =  0, min = 0, wday = 5, day = 1, nsec = 0,
        isdst = false, yday = 1, tzoffset = 0, month = 1,
        year = 1970, hour = 0
    }
    test:is_deeply(local_totable(ts), table_expected, "correct :totable")
    local date_expected = date.new()
    date_expected.epoch = 1
    test:is(local_set(ts, {sec = 1}), date_expected, "correct :set")

    -- check results of wrong arguments passed
    assert_raises(test, exp_datetime("datetime.format()", 123),
                  function() return local_format(123) end)
    assert_raises(test, exp_datetime("datetime.format()", "1970-01-01"),
                  function() return local_format("1970-01-01") end)
    assert_raises(test, exp_datetime("datetime.format()"),
                  function() return local_format() end)

    assert_raises(test, exp_datetime("datetime.totable()", 123),
                  function() return local_totable(123) end)
    assert_raises(test, exp_datetime("datetime.totable()", "1970-01-01"),
                  function() return local_totable("1970-01-01") end)
    assert_raises(test, exp_datetime("datetime.totable()"),
                  function() return local_totable() end)

    assert_raises(test, exp_datetime("datetime.set()", 123),
                  function() return local_set(123) end)
    assert_raises(test, exp_datetime("datetime.set()", "1970-01-01"),
                  function() return local_set("1970-01-01") end)
    assert_raises(test, exp_datetime("datetime.set()"),
                  function() return local_set() end)
end)

test:test("Default date creation and comparison", function(test)
    test:plan(37)
    -- check empty arguments
    local ts1 = date.new()
    test:is(ts1.epoch, 0, "ts.epoch ==0")
    test:is(ts1.nsec, 0, "ts.nsec == 0")
    test:is(ts1.tzoffset, 0, "ts.tzoffset == 0")
    test:is(tostring(ts1), "1970-01-01T00:00:00Z", "tostring(ts1)")
    -- check empty table
    local ts2 = date.new{}
    test:is(ts2.epoch, 0, "ts.epoch ==0")
    test:is(ts2.nsec, 0, "ts.nsec == 0")
    test:is(ts2.tzoffset, 0, "ts.tzoffset == 0")
    test:is(tostring(ts2), "1970-01-01T00:00:00Z", "tostring(ts2)")
    -- check their equivalence
    test:is(ts1, ts2, "ts1 == ts2")
    test:is(ts1 ~= ts2, false, "not ts1 != ts2")

    test:isnt(ts1, nil, "ts1 != {}")
    test:isnt(ts1, {}, "ts1 != {}")
    test:isnt(ts1, "1970-01-01T00:00:00Z", "ts1 != '1970-01-01T00:00:00Z'")
    test:isnt(ts1, 19700101, "ts1 != 19700101")

    test:isnt(nil, ts1, "{} ~= ts1")
    test:isnt({}, ts1 ,"{} ~= ts1")
    test:isnt("1970-01-01T00:00:00Z", ts1, "'1970-01-01T00:00' ~= ts1")
    test:isnt(19700101, ts1, "ts1 ~= ts1")

    test:is(ts1 < ts2, false, "not ts1 < ts2")
    test:is(ts1 > ts2, false, "not ts1 < ts2")
    test:is(ts1 <= ts2, true, "not ts1 < ts2")
    test:is(ts1 >= ts2, true, "not ts1 < ts2")

    -- check is_datetime
    test:is(date.is_datetime(ts1), true, "ts1 is datetime")
    test:is(date.is_datetime(ts2), true, "ts2 is datetime")
    test:is(date.is_datetime({}), false, "bogus is not datetime")

    -- check comparison errors -- ==, ~= not raise errors, other
    -- comparison operators should raise
    assert_raises(test, incompat_types, function() return ts1 < nil end)
    assert_raises(test, incompat_types, function() return ts1 < 123 end)
    assert_raises(test, incompat_types, function() return ts1 < '1970-01-01' end)
    assert_raises(test, incompat_types, function() return ts1 <= nil end)
    assert_raises(test, incompat_types, function() return ts1 <= 123 end)
    assert_raises(test, incompat_types, function() return ts1 <= '1970-01-01' end)
    assert_raises(test, incompat_types, function() return ts1 > nil end)
    assert_raises(test, incompat_types, function() return ts1 > 123 end)
    assert_raises(test, incompat_types, function() return ts1 > '1970-01-01' end)
    assert_raises(test, incompat_types, function() return ts1 >= nil end)
    assert_raises(test, incompat_types, function() return ts1 >= 123 end)
    assert_raises(test, incompat_types, function() return ts1 >= '1970-01-01' end)
end)

test:test("Simple date creation by attributes", function(test)
    test:plan(12)
    local ts
    local obj = {}
    local attribs = {
        { 'year', 2000, '2000-01-01T00:00:00Z' },
        { 'month', 11, '2000-11-01T00:00:00Z' },
        { 'day', 30, '2000-11-30T00:00:00Z' },
        { 'hour', 6, '2000-11-30T06:00:00Z' },
        { 'min', 12, '2000-11-30T06:12:00Z' },
        { 'sec', 23, '2000-11-30T06:12:23Z' },
        { 'tzoffset', -8*60, '2000-11-30T06:12:23-0800' },
        { 'tzoffset', '+0800', '2000-11-30T06:12:23+0800' },
    }
    for _, row in pairs(attribs) do
        local key, value, str = unpack(row)
        obj[key] = value
        ts = date.new(obj)
        test:is(tostring(ts), str, ('{%s = %s}, expected %s'):
                format(key, value, str))
    end
    test:is(tostring(date.new{timestamp = 1630359071.125}),
            '2021-08-30T21:31:11.125Z', '{timestamp}')
    test:is(tostring(date.new{timestamp = 1630359071, msec = 123}),
            '2021-08-30T21:31:11.123Z', '{timestamp.msec}')
    test:is(tostring(date.new{timestamp = 1630359071, usec = 123}),
            '2021-08-30T21:31:11.000123Z', '{timestamp.usec}')
    test:is(tostring(date.new{timestamp = 1630359071, nsec = 123}),
            '2021-08-30T21:31:11.000000123Z', '{timestamp.nsec}')
end)

test:test("Simple date creation by attributes - check failed", function(test)
    test:plan(84)

    local boundary_checks = {
        {'year', {MIN_DATE_YEAR, MAX_DATE_YEAR}},
        {'month', {1, 12}},
        {'day', {1, 31, -1}},
        {'hour', {0, 23}},
        {'min', {0, 59}},
        {'sec', {0, 60}},
        {'usec', {0, 1e6}},
        {'msec', {0, 1e3}},
        {'nsec', {0, 1e9}},
        {'tzoffset', {-720, 840}, str_or_num_exp},
    }
    local ts = date.new()

    for _, row in pairs(boundary_checks) do
        local attr_name, bounds, expected_msg = unpack(row)
        local left, right, extra = unpack(bounds)

        if extra == nil then
            assert_raises(test,
                          range_check_error(attr_name, left - 1,
                          {left, right}),
                          function() date.new{ [attr_name] = left - 1} end)
            assert_raises(test,
                          range_check_error(attr_name, right + 1,
                          {left, right}),
                          function() date.new{ [attr_name] = right + 1} end)
            assert_raises(test,
                          range_check_error(attr_name, left - 50,
                          {left, right}),
                          function() date.new{ [attr_name] = left - 50} end)
            assert_raises(test,
                          range_check_error(attr_name, right + 50,
                          {left, right}),
                          function() date.new{ [attr_name] = right + 50} end)
        else -- special case for {day = -1}
            assert_raises(test,
                          range_check_3_error(attr_name, left - 1,
                          {extra, left, right}),
                          function() date.new{ [attr_name] = left - 1} end)
            assert_raises(test,
                          range_check_3_error(attr_name, right + 1,
                          {extra, left, right}),
                          function() date.new{ [attr_name] = right + 1} end)
            assert_raises(test,
                          range_check_3_error(attr_name, left - 50,
                          {extra, left, right}),
                          function() date.new{ [attr_name] = left - 50} end)
            assert_raises(test,
                          range_check_3_error(attr_name, right + 50,
                          {extra, left, right}),
                          function() date.new{ [attr_name] = right + 50} end)
        end
        -- tzoffset uses different message to others
        expected_msg = expected_msg or numeric_exp
        assert_raises_like(test, expected_msg,
                           function() date.new{[attr_name] = {}} end)
        assert_raises_like(test, expected_msg,
                           function() date.new{[attr_name] = ts} end)
    end

    local specific_errors = {
        {only_one_of, { nsec = 123456, usec = 123}},
        {only_one_of, { nsec = 123456, msec = 123}},
        {only_one_of, { usec = 123, msec = 123}},
        {only_one_of, { nsec = 123456, usec = 123, msec = 123}},
        {only_integer_ts, { timestamp = 12345.125, usec = 123}},
        {only_integer_ts, { timestamp = 12345.125, msec = 123}},
        {only_integer_ts, { timestamp = 12345.125, nsec = 123}},
        {timestamp_and_ymd, {timestamp = 1630359071.125, month = 9 }},
        {timestamp_and_ymd, {timestamp = 1630359071.125, month = 9 }},
        {timestamp_and_ymd, {timestamp = 1630359071.125, day = 29 }},
        {timestamp_and_hms, {timestamp = 1630359071.125, hour = 20 }},
        {timestamp_and_hms, {timestamp = 1630359071.125, min = 10 }},
        {timestamp_and_hms, {timestamp = 1630359071.125, sec = 29 }},
        {nyi_error('tz'), {tz = 400}},
        {table_expected('datetime.new()', '2001-01-01'), '2001-01-01'},
        {table_expected('datetime.new()', 20010101), 20010101},
        {range_check_3_error('day', 32, {-1, 1, 31}),
            {year = 2021, month = 6, day = 32}},
        {invalid_days_in_mon(31, 6, 2021), { year = 2021, month = 6, day = 31}},
        {less_than_min(-5879610, 6, 21),
            {year = -5879610, month = 6, day = 21}},
        {less_than_min(-5879610, 1, 1),
            {year = -5879610, month = 1, day = 1}},
        {range_check_error('year', -16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = -16009610, month = 12, day = 31}},
        {range_check_error('year', 16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = 16009610, month = 1, day = 1}},
        {greater_than_max(5879611, 9, 1),
            {year = 5879611, month = 9, day = 1}},
        {greater_than_max(5879611, 7, 12),
            {year = 5879611, month = 7, day = 12}},
    }
    for _, row in pairs(specific_errors) do
        local err_msg, attribs = unpack(row)
        print(require'json'.encode(attribs))
        assert_raises(test, err_msg, function() date.new(attribs) end)
    end
end)

test:test("Formatting limits", function(test)
    test:plan(6)
    local ts = date.new()
    local len = ffi.C.tnt_datetime_to_string(ts, nil, 0)
    test:is(len, 20, 'tostring() with NULL')
    local buff = ffi.new('char[?]', len + 1)
    len = ffi.C.tnt_datetime_to_string(ts, buff, len + 1)
    test:is(len, 20, 'tostring() with non-NULL')
    test:is(ffi.string(buff), '1970-01-01T00:00:00Z', 'Epoch string')

    local fmt = '%d/%m/%Y'
    len = ffi.C.tnt_datetime_strftime(ts, nil, 0, fmt)
    test:is(len, 0, 'format(fmt) with NULL')
    local strfmt_sz = 128
    buff = ffi.new('char[?]', strfmt_sz)
    len = ffi.C.tnt_datetime_strftime(ts, buff, strfmt_sz, fmt)
    test:is(len, 10, 'format(fmt) with non-NULL')
    test:is(ffi.string(buff), '01/01/1970', 'Epoch string (fmt)')
end)

test:test("Datetime string formatting", function(test)
    test:plan(8)
    local t = date.new()
    test:is(t.epoch, 0, ('t.epoch == %d'):format(tonumber(t.epoch)))
    test:is(t.nsec, 0, ('t.nsec == %d'):format(t.nsec))
    test:is(t.tzoffset, 0, ('t.tzoffset == %d'):format(t.tzoffset))
    test:is(t:format('%d/%m/%Y'), '01/01/1970', '%s: format #1')
    test:is(t:format('%A %d. %B %Y'), 'Thursday 01. January 1970', 'format #2')
    test:is(t:format('%FT%T'), '1970-01-01T00:00:00', 'format #3')
    test:is(t:format(), '1970-01-01T00:00:00Z', 'format #6')
    assert_raises(test, expected_str('datetime.strftime()', 1234),
                  function() t:format(1234) end)
end)

test:test("__index functions()", function(test)
    test:plan(15)
    -- 2000-01-29T03:30:12Z'
    local ts = date.new{sec = 12, min = 30, hour = 3,
                       tzoffset = 0,  day = 29, month = 1, year = 2000,
                       nsec = 123000000}

    test:is(ts.year, 2000, 'ts.year')
    test:is(ts.yday, 29, 'ts.yday')
    test:is(ts.month, 1, 'ts.month')
    test:is(ts.day, 29, 'ts.day')
    test:is(ts.wday, 7, 'ts.wday')
    test:is(ts.min, 30, 'ts.min')
    test:is(ts.hour, 3, 'ts.hour')
    test:is(ts.min, 30, 'ts.min')
    test:is(ts.sec, 12, 'ts.sec')
    test:is(ts.isdst, false, "ts.isdst")
    test:is(ts.tzoffset, 0, "ts.tzoffset")
    test:is(ts.timestamp, 949116612.123, "ts.timestamp")

    test:is(ts.nsec, 123000000, 'ts.nsec')
    test:is(ts.usec, 123000, 'ts.usec')
    test:is(ts.msec, 123, 'ts.msec')
end)

test:test("totable{}", function(test)
    test:plan(78)
    local exp = {sec = 0, min = 0, wday = 5, day = 1,
                 nsec = 0, isdst = false, yday = 1,
                 tzoffset = 0, month = 1, year = 1970, hour = 0}
    local ts = date.new()
    local totable = ts:totable()
    test:is_deeply(totable, exp, 'date:totable()')

    local osdate = os.date('*t')
    totable = date.new(osdate):totable()
    local keys = {
        'sec', 'min', 'wday', 'day', 'yday', 'month', 'year', 'hour'
    }
    for _, key in pairs(keys) do
        test:is(totable[key], osdate[key],
                ('[%s]: %s == %s'):format(key, totable[key], osdate[key]))
    end
    for tst_d = 21,28 do
        -- check wday wrapping for the whole week
        osdate = os.date('*t', os.time{year = 2021, month = 9, day = tst_d})
        totable = date.new(osdate):totable()
        for _, key in pairs(keys) do
            test:is(totable[key], osdate[key],
                    ('[%s]: %s == %s'):format(key, totable[key], osdate[key]))
        end
    end
    -- date.now() and os.date('*t') could span day boundary in between their
    -- invocations. If midnight suddenly happened - simply call them both again
    ts = date.now() osdate = os.date('*t')
    if ts.day ~= osdate.day then
        ts = date.now() osdate = os.date('*t')
    end
    for _, key in pairs({'wday', 'day', 'yday', 'month', 'year'}) do
        test:is(ts[key], osdate[key],
                ('[%s]: %s == %s'):format(key, ts[key], osdate[key]))
    end
end)

test:test("Time :set{} operations", function(test)
    test:plan(12)

    local ts = date.new{ year = 2021, month = 8, day = 31,
                  hour = 0, min = 31, sec = 11, tzoffset = '+0300'}
    test:is(tostring(ts), '2021-08-31T00:31:11+0300', 'initial')
    test:is(tostring(ts:set{ year = 2020 }), '2020-08-31T00:31:11+0300',
            '2020 year')
    test:is(tostring(ts:set{ month = 11, day = 30 }),
            '2020-11-30T00:31:11+0300', 'month = 11, day = 30')
    test:is(tostring(ts:set{ day = 9 }), '2020-11-09T00:31:11+0300',
            'day 9')
    test:is(tostring(ts:set{ hour = 6 }),  '2020-11-09T06:31:11+0300',
            'hour 6')
    test:is(tostring(ts:set{ min = 12, sec = 23 }), '2020-11-09T04:12:23+0300',
            'min 12, sec 23')
    test:is(tostring(ts:set{ tzoffset = -8*60 }), '2020-11-08T17:12:23-0800',
            'offset -0800' )
    test:is(tostring(ts:set{ tzoffset = '+0800' }), '2020-11-09T09:12:23+0800',
            'offset +0800' )
    test:is(tostring(ts:set{ timestamp = 1630359071.125 }),
            '2021-08-31T05:31:11.125+0800', 'timestamp 1630359071.125' )
    test:is(tostring(ts:set{ msec = 123}), '2021-08-31T05:31:11.123+0800',
            'msec = 123')
    test:is(tostring(ts:set{ usec = 123}), '2021-08-31T05:31:11.000123+0800',
            'usec = 123')
    test:is(tostring(ts:set{ nsec = 123}), '2021-08-31T05:31:11.000000123+0800',
            'nsec = 123')
end)

test:test("Time invalid :set{} operations", function(test)
    test:plan(84)

    local boundary_checks = {
        {'year', {MIN_DATE_YEAR, MAX_DATE_YEAR}},
        {'month', {1, 12}},
        {'day', {1, 31, -1}},
        {'hour', {0, 23}},
        {'min', {0, 59}},
        {'sec', {0, 60}},
        {'usec', {0, 1e6}},
        {'msec', {0, 1e3}},
        {'nsec', {0, 1e9}},
        {'tzoffset', {-720, 840}, str_or_num_exp},
    }
    local ts = date.new()

    for _, row in pairs(boundary_checks) do
        local attr_name, bounds, expected_msg = unpack(row)
        local left, right, extra = unpack(bounds)

        if extra == nil then
            assert_raises(test,
                          range_check_error(attr_name, left - 1,
                          {left, right}),
                          function() ts:set{ [attr_name] = left - 1} end)
            assert_raises(test,
                          range_check_error(attr_name, right + 1,
                          {left, right}),
                          function() ts:set{ [attr_name] = right + 1} end)
            assert_raises(test,
                          range_check_error(attr_name, left - 50,
                          {left, right}),
                          function() ts:set{ [attr_name] = left - 50} end)
            assert_raises(test,
                          range_check_error(attr_name, right + 50,
                          {left, right}),
                          function() ts:set{ [attr_name] = right + 50} end)
        else -- special case for {day = -1}
            assert_raises(test,
                          range_check_3_error(attr_name, left - 1,
                          {extra, left, right}),
                          function() ts:set{ [attr_name] = left - 1} end)
            assert_raises(test,
                          range_check_3_error(attr_name, right + 1,
                          {extra, left, right}),
                          function() ts:set{ [attr_name] = right + 1} end)
            assert_raises(test,
                          range_check_3_error(attr_name, left - 50,
                          {extra, left, right}),
                          function() ts:set{ [attr_name] = left - 50} end)
            assert_raises(test,
                          range_check_3_error(attr_name, right + 50,
                          {extra, left, right}),
                          function() ts:set{ [attr_name] = right + 50} end)
        end
        -- tzoffset uses different message to others
        expected_msg = expected_msg or numeric_exp
        assert_raises_like(test, expected_msg,
                           function() ts:set{[attr_name] = {}} end)
        assert_raises_like(test, expected_msg,
                           function() ts:set{[attr_name] = ts} end)
    end

    ts:set{year = 2021}
    local specific_errors = {
        {only_one_of, { nsec = 123456, usec = 123}},
        {only_one_of, { nsec = 123456, msec = 123}},
        {only_one_of, { usec = 123, msec = 123}},
        {only_one_of, { nsec = 123456, usec = 123, msec = 123}},
        {only_integer_ts, { timestamp = 12345.125, usec = 123}},
        {only_integer_ts, { timestamp = 12345.125, msec = 123}},
        {only_integer_ts, { timestamp = 12345.125, nsec = 123}},
        {timestamp_and_ymd, {timestamp = 1630359071.125, month = 9 }},
        {timestamp_and_ymd, {timestamp = 1630359071.125, month = 9 }},
        {timestamp_and_ymd, {timestamp = 1630359071.125, day = 29 }},
        {timestamp_and_hms, {timestamp = 1630359071.125, hour = 20 }},
        {timestamp_and_hms, {timestamp = 1630359071.125, min = 10 }},
        {timestamp_and_hms, {timestamp = 1630359071.125, sec = 29 }},
        {nyi_error('tz'), {tz = 400}},
        {table_expected('datetime.set()', '2001-01-01'), '2001-01-01'},
        {table_expected('datetime.set()', 20010101), 20010101},
        {range_check_3_error('day', 32, {-1, 1, 31}),
            {year = 2021, month = 6, day = 32}},
        {invalid_days_in_mon(31, 6, 2021), { month = 6, day = 31}},
        {less_than_min(-5879610, 6, 21),
            {year = -5879610, month = 6, day = 21}},
        {less_than_min(-5879610, 1, 1),
            {year = -5879610, month = 1, day = 1}},
        {range_check_error('year', -16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = -16009610, month = 12, day = 31}},
        {range_check_error('year', 16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = 16009610, month = 1, day = 1}},
        {greater_than_max(5879611, 9, 1),
            {year = 5879611, month = 9, day = 1}},
        {greater_than_max(5879611, 7, 12),
            {year = 5879611, month = 7, day = 12}},
    }
    for _, row in pairs(specific_errors) do
        local err_msg, attribs = unpack(row)
        assert_raises(test, err_msg, function() ts:set(attribs) end)
    end
end)

test:test("Time invalid tzoffset in :set{} operations", function(test)
    test:plan(14)

    local ts = date.new{}
    local bad_strings = {
        '+03:00 what?',
        '-0000 ',
        '+0000 ',
        'bogus',
        '0100',
        '+-0100',
        '+25:00',
        '+9900',
        '-99:00',
    }
    for _, val in ipairs(bad_strings) do
        assert_raises(test, invalid_tz_fmt_error(val),
                      function() ts:set{ tzoffset = val } end)
    end

    local bad_numbers = {
        880,
        -800,
        10000,
        -10000,
    }
    for _, val in ipairs(bad_numbers) do
        assert_raises(test, range_check_error('tzoffset', val, {-720, 840}),
                      function() ts:set{ tzoffset = val } end)
    end
    assert_raises(test, nyi_error('tz'), function() ts:set{tz = 400} end)
end)

test:test("Time :set{day = -1} operations", function(test)
    test:plan(8)
    local tests = {
        {{ year = 2000, month = 3, day = -1}, '2000-03-31T00:00:00Z'},
        {{ year = 2000, month = 2, day = -1}, '2000-02-29T00:00:00Z'},
        {{ year = 2001, month = 2, day = -1}, '2001-02-28T00:00:00Z'},
        {{ year = 1900, month = 2, day = -1}, '1900-02-28T00:00:00Z'},
        {{ year = 1904, month = 2, day = -1}, '1904-02-29T00:00:00Z'},
    }
    local ts
    for _, row in ipairs(tests) do
        local args, str = unpack(row)
        ts = date.new(args)
        test:is(tostring(ts), str, ('checking -1 with %s'):format(str))
    end

    ts = date.new{ year = 1904, month = 2, day = -1 }
    test:is(tostring(ts), '1904-02-29T00:00:00Z', 'base before :set{}')
    test:is(tostring(ts:set{month = 3, day = 2}), '1904-03-02T00:00:00Z',
            '2 March')
    test:is(tostring(ts:set{day = -1}), '1904-03-31T00:00:00Z', '31 March')
end)

os.exit(test:check() and 0 or 1)
