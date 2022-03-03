#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('errno')
local date = require('datetime')
local ffi = require('ffi')

--[[
    Workaround for #6599 where we may randomly fail on AWS Graviton machines,
    while it's working properly when jit disabled.
--]]
if jit.arch == 'arm64' then jit.off() end

test:plan(23)

-- minimum supported date - -5879610-06-22
local MIN_DATE_YEAR = -5879610
local MIN_DATE_MONTH = 6
local MIN_DATE_DAY = 22
-- maximum supported date - 5879611-07-11
local MAX_DATE_YEAR = 5879611
local MAX_DATE_MONTH = 7
local MAX_DATE_DAY = 11

local SECS_PER_DAY      = 86400
local AVERAGE_DAYS_YEAR = 365.25
local AVERAGE_WEEK_YEAR = AVERAGE_DAYS_YEAR / 7
local MAX_YEAR_RANGE = MAX_DATE_YEAR - MIN_DATE_YEAR
local MAX_MONTH_RANGE = MAX_YEAR_RANGE * 12
local MAX_WEEK_RANGE = MAX_YEAR_RANGE * AVERAGE_WEEK_YEAR
local MAX_DAY_RANGE = MAX_YEAR_RANGE * AVERAGE_DAYS_YEAR
local MAX_HOUR_RANGE = MAX_DAY_RANGE * 24
local MAX_MIN_RANGE = MAX_HOUR_RANGE * 60
local MAX_SEC_RANGE = MAX_DAY_RANGE * SECS_PER_DAY

local incompat_types = 'incompatible types for datetime comparison'
local only_integer_ts = 'only integer values allowed in timestamp'..
                        ' if nsec, usec, or msecs provided'
local only_one_of = 'only one of nsec, usec or msecs may be defined'..
                    ' simultaneously'
local int_ival_exp = 'sec: integer value expected, but received number'
local timestamp_and_ymd = 'timestamp is not allowed if year/month/day provided'
local timestamp_and_hms = 'timestamp is not allowed if hour/min/sec provided'
local str_or_num_exp = 'tzoffset: string or number expected, but received'
local numeric_exp = 'numeric value expected, but received '
local expected_interval_but = 'expected interval or table, but received'
local expected_datetime_but = 'expected datetime, interval or table, but received'

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
    return ('value %s of %s is out of allowed range [%s, %s]'):
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
        assert_raises(test, err_msg, function() date.new(attribs) end)
    end
end)

test:test("Formatting limits", function(test)
    test:plan(15)
    local ts = date.new()
    local len = ffi.C.tnt_datetime_to_string(ts, nil, 0)
    test:is(len, 20, 'tostring() with NULL')
    local buff = ffi.new('char[?]', len + 1)
    len = ffi.C.tnt_datetime_to_string(ts, buff, len + 1)
    test:is(len, 20, 'tostring() with non-NULL')
    test:is(ffi.string(buff), '1970-01-01T00:00:00Z', 'Epoch string')

    local fmt = '%d/%m/%Y'
    len = ffi.C.tnt_datetime_strftime(ts, nil, 0, fmt)
    test:is(len, 10, 'format(fmt) with NULL')
    local strfmt_sz = 128
    buff = ffi.new('char[?]', strfmt_sz)
    len = ffi.C.tnt_datetime_strftime(ts, buff, strfmt_sz, fmt)
    test:is(len, 10, 'format(fmt) with non-NULL')
    test:is(ffi.string(buff), '01/01/1970', 'Epoch string (fmt)')

    local bogus_fmt = string.rep('1', 10000)

    len = ffi.C.tnt_datetime_strftime(ts, nil, 0, bogus_fmt)
    test:is(len, #bogus_fmt, 'format(fmt) with NULL and bogus format')

    local s = ts:format(bogus_fmt)
    test:is(s, bogus_fmt, 'bogus format #1')
    s = ts:format(bogus_fmt..'%T')
    test:is(s, bogus_fmt..'00:00:00', 'bogus format #2')

    -- checks for boundary conditions in datetime_strftime
    -- which uses 128-bytes long stash buffer for fast-track allocations
    for len = 127,129 do
        bogus_fmt = string.rep('1', len)
        s = ts:format(bogus_fmt)
        test:is(len, #bogus_fmt, ('bogus format lengths check %d'):format(len))
        test:is(s, bogus_fmt, 'bogus format equality check')
    end
end)

test:test("Datetime string formatting", function(test)
    test:plan(10)
    local t = date.new()
    test:is(t.epoch, 0, ('t.epoch == %d'):format(tonumber(t.epoch)))
    test:is(t.nsec, 0, ('t.nsec == %d'):format(t.nsec))
    test:is(t.tzoffset, 0, ('t.tzoffset == %d'):format(t.tzoffset))
    test:is(t:format('%d/%m/%Y'), '01/01/1970', '%s: format #1')
    test:is(t:format('%A %d. %B %Y'), 'Thursday 01. January 1970', 'format #2')
    test:is(t:format('%FT%T%z'), '1970-01-01T00:00:00+0000', 'format #3')
    test:is(t:format('%FT%T.%f%z'), '1970-01-01T00:00:00.000+0000', 'format #4')
    test:is(t:format('%FT%T.%4f%z'), '1970-01-01T00:00:00.0000+0000', 'format #5')
    test:is(t:format(), '1970-01-01T00:00:00Z', 'format #6')
    assert_raises(test, expected_str('datetime.strftime()', 1234),
                  function() t:format(1234) end)
end)

local function check_variant_formats(test, base, variants)
    for _, var in pairs(variants) do
        local year, str, strf = unpack(var)
        local obj = base
        obj.year = year
        local t = date.new(obj)
        test:is(t:format(), str, ('default format for year = %d'):format(year))
        test:is(t:format('%FT%T.%f%z'), strf,
                ('strftime for year = %d'):format(year))
    end
end

test:test("Datetime formatting of huge dates", function(test)
    test:plan(22)
    local base = {month = 6, day = 10, hour = 12, min = 10, sec = 10}
    local variants = {
        {-5000000, '-5000000-06-10T12:10:10Z', '-5000000-06-10T12:10:10.000+0000'},
        {-10000, '-10000-06-10T12:10:10Z', '-10000-06-10T12:10:10.000+0000'},
        {-1, '-001-06-10T12:10:10Z', '-001-06-10T12:10:10.000+0000'},
        {1, '0001-06-10T12:10:10Z', '0001-06-10T12:10:10.000+0000'},
        {10, '0010-06-10T12:10:10Z', '0010-06-10T12:10:10.000+0000'},
        {10000, '10000-06-10T12:10:10Z', '10000-06-10T12:10:10.000+0000'},
        {5000000, '5000000-06-10T12:10:10Z', '5000000-06-10T12:10:10.000+0000'},
    }
    check_variant_formats(test, base, variants)

    local base = {month = 10, day = 8}
    local variants = {
        {-10000, '-10000-10-08T00:00:00Z', '-10000-10-08T00:00:00.000+0000'},
        {-1, '-001-10-08T00:00:00Z', '-001-10-08T00:00:00.000+0000'},
        {1, '0001-10-08T00:00:00Z', '0001-10-08T00:00:00.000+0000'},
        {10000, '10000-10-08T00:00:00Z', '10000-10-08T00:00:00.000+0000'},
    }
    check_variant_formats(test, base, variants)
end)

test:test("Datetime string formatting detailed", function(test)
    test:plan(77)
    local T = date.new{ timestamp = 0.125 }
    T:set{ tzoffset = 180 }
    test:is(tostring(T), '1970-01-01T03:00:00.125+0300', 'tostring()')

    local formats = {
        { '%A',                         'Thursday' },
        { '%a',                         'Thu' },
        { '%B',                         'January' },
        { '%b',                         'Jan' },
        { '%h',                         'Jan' },
        { '%C',                         '19' },
        { '%c',                         'Thu Jan  1 03:00:00 1970' },
        { '%D',                         '01/01/70' },
        { '%m/%d/%y',                   '01/01/70' },
        { '%d',                         '01' },
        { '%Ec',                        'Thu Jan  1 03:00:00 1970' },
        { '%EC',                        '19' },
        { '%Ex',                        '01/01/70' },
        { '%EX',                        '03:00:00' },
        { '%Ey',                        '70' },
        { '%EY',                        '1970' },
        { '%Od',                        '01' },
        { '%oe',                        'oe' },
        { '%OH',                        '03' },
        { '%OI',                        '03' },
        { '%Om',                        '01' },
        { '%OM',                        '00' },
        { '%OS',                        '00' },
        { '%Ou',                        '4' },
        { '%OU',                        '00' },
        { '%OV',                        '01' },
        { '%Ow',                        '4' },
        { '%OW',                        '00' },
        { '%Oy',                        '70' },
        { '%e',                         ' 1' },
        { '%F',                         '1970-01-01' },
        { '%Y-%m-%d',                   '1970-01-01' },
        { '%H',                         '03' },
        { '%I',                         '03' },
        { '%j',                         '001' },
        { '%k',                         ' 3' },
        { '%l',                         ' 3' },
        { '%M',                         '00' },
        { '%m',                         '01' },
        { '%n',                         '\n' },
        { '%p',                         'AM' },
        { '%R',                         '03:00' },
        { '%H:%M',                      '03:00' },
        { '%r',                         '03:00:00 AM' },
        { '%I:%M:%S %p',                '03:00:00 AM' },
        { '%S',                         '00' },
        { '%s',                         '10800' },
        { '%f',                         '125' },
        { '%3f',                        '125' },
        { '%6f',                        '125000' },
        { '%6d',                        '6d' },
        { '%3D',                        '3D' },
        { '%T',                         '03:00:00' },
        { '%H:%M:%S',                   '03:00:00' },
        { '%t',                         '\t' },
        { '%U',                         '00' },
        { '%u',                         '4' },
        { '%V',                         '01' },
        { '%G',                         '1970' },
        { '%g',                         '70' },
        { '%v',                         ' 1-Jan-1970' },
        { '%e-%b-%Y',                   ' 1-Jan-1970' },
        { '%W',                         '00' },
        { '%w',                         '4' },
        { '%X',                         '03:00:00' },
        { '%x',                         '01/01/70' },
        { '%y',                         '70' },
        { '%Y',                         '1970' },
        { '%z',                         '+0300' },
        { '%%',                         '%' },
        { '%Y-%m-%dT%H:%M:%S.%9f%z',    '1970-01-01T03:00:00.125000000+0300' },
        { '%Y-%m-%dT%H:%M:%S.%f%z',     '1970-01-01T03:00:00.125+0300' },
        { '%Y-%m-%dT%H:%M:%S.%f',       '1970-01-01T03:00:00.125' },
        { '%FT%T.%f',                   '1970-01-01T03:00:00.125' },
        { '%FT%T.%f%z',                 '1970-01-01T03:00:00.125+0300' },
        { '%FT%T.%9f%z',                '1970-01-01T03:00:00.125000000+0300' },
    }
    for _, row in pairs(formats) do
        local fmt, value = unpack(row)
        test:is(T:format(fmt), value,
                ('format %s, expected %s'):format(fmt, value))
    end
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

test:test("Time interval tostring()", function(test)
    test:plan(19)
    local ivals = {
        {{sec = 1}, '+1 seconds'},
        {{sec = 49}, '+49 seconds'},
        {{min = 10, sec = 30}, '+10 minutes, 30 seconds'},
        {{hour = 1, min = 59, sec = 10}, '+1 hours, 59 minutes, 10 seconds'},
        {{hour = 12, min = 10, sec = 30}, '+12 hours, 10 minutes, 30 seconds'},
        {{day = 2, hour = 8, min = 10, sec = 30},
         '+2 days, 8 hours, 10 minutes, 30 seconds'},
        {{week = 10, hour = 8, min = 10, sec = 30},
         '+70 days, 8 hours, 10 minutes, 30 seconds'},
        {{month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+20 months, 70 days, 8 hours, 10 minutes, 30 seconds'},
        {{year = 10, month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+10 years, 20 months, 70 days, 8 hours, 10 minutes, 30 seconds'},
        {{year = 1e4, month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+10000 years, 20 months, 70 days, 8 hours, 10 minutes, 30 seconds'},
        {{year = 5e6, month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+5000000 years, 20 months, 70 days, 8 hours, 10 minutes, 30 seconds'},
        {{year = -5e6, month = -20, week = -10, hour = -8, min = -10, sec = -30},
         '-5000000 years, -20 months, -70 days, -8 hours, -10 minutes, -30 seconds'},
        {{month = -20, week = -10, hour = -8, min = -10, sec = -30},
         '-20 months, -70 days, -8 hours, -10 minutes, -30 seconds'},
        {{week = -10, hour = -8, min = -10, sec = -30},
         '-70 days, -8 hours, -10 minutes, -30 seconds'},
        {{hour = -12, min = -10, sec = -30}, '-12 hours, -10 minutes, -30 seconds'},
        {{hour = -1, min = -59, sec = -10}, '-1 hours, -59 minutes, -10 seconds'},
        {{min = -10, sec = -30}, '-10 minutes, -30 seconds'},
        {{sec = -49}, '-49 seconds'},
        {{sec = -1}, '-1 seconds'},
    }
    for _, row in pairs(ivals) do
        local ival_init, str = unpack(row)
        local ival = date.interval.new(ival_init)
        test:is(tostring(ival), str, str)
    end
end)

test:test("Time interval __index fields", function(test)
    test:plan(11)

    local ival = date.interval.new{year = 12345, month = 123, week = 100,
                                   day = 45, hour = 48, min = 3, sec = 1,
                                   nsec = 12345678}
    test:is(tostring(ival), '+12345 years, 123 months, 747 days, 0 hours,'..
            ' 3 minutes, 1.012345678 seconds', '__tostring')

    test:is(ival.nsec, 12345678, 'nsec')
    test:is(ival.usec, 12345, 'usec')
    test:is(ival.msec, 12, 'msec')

    test:is(ival.year, 12345, 'interval.year')
    test:is(ival.month, 123, 'interval.month')
    test:is(ival.week, 106, 'interval.week')
    test:is(ival.day, 747, 'interval.day')
    test:is(ival.hour, 17928, 'interval.hour')
    test:is(ival.min, 1075683, 'interval.min')
    test:is(ival.sec, 64540981, 'interval.sec')
end)

test:test("Time interval operations", function(test)
    test:plan(164)

    local ival_new = date.interval.new

    -- check arithmetic with leap dates
    local base_leap_date = { year = 1972, month = 2, day = 29}

    local leap_ivals_check = {
        {{month = 1}, '1972-03-29T00:00:00Z', '1972-01-29T00:00:00Z'},
        {{month = 1}, '1972-04-29T00:00:00Z', '1971-12-29T00:00:00Z'},
        {{month = 5}, '1972-09-29T00:00:00Z', '1971-07-29T00:00:00Z'},
        {{year = 1, month = 2}, '1973-11-29T00:00:00Z', '1970-05-29T00:00:00Z'},
        {{year = 2, month = 3}, '1976-02-29T00:00:00Z', '1968-02-29T00:00:00Z'},
        {{month = 1}, '1976-03-29T00:00:00Z', '1968-01-29T00:00:00Z'},
        {{month = 1}, '1976-04-29T00:00:00Z', '1967-12-29T00:00:00Z'},
        {{month = 5}, '1976-09-29T00:00:00Z', '1967-07-29T00:00:00Z'},
        {{year = -1}, '1975-09-29T00:00:00Z', '1968-07-29T00:00:00Z'},
        {{year = -1}, '1974-09-29T00:00:00Z', '1969-07-29T00:00:00Z'},
    }

    local tadd_t = date.new(base_leap_date) -- :add{table}
    local tadd_i = date.new(base_leap_date) -- :add{interval}
    local tplus_i = date.new(base_leap_date) -- t + interval
    local tplus_t = date.new(base_leap_date) -- t + {table}

    -- check additions
    for _, row in pairs(leap_ivals_check) do
        local delta, sadd, _ = unpack(row)
        -- t:add{table}
        test:is(tostring(tadd_t:add(delta)), sadd,
                ('add delta to %s'):format(sadd))
        -- t:add(interval)
        test:is(tostring(tadd_i:add(ival_new(delta))), sadd,
                ('add ival delta to %s'):format(sadd))
        -- t + interval
        tplus_i = tplus_i + ival_new(delta)
        test:is(tostring(tplus_i), sadd, ('%s: t + ival delta'):format(sadd))
        -- t - interval
        tplus_t = tplus_t + delta
        test:is(tostring(tplus_t), sadd, ('%s: t + table delta'):format(sadd))
    end

    local tsub_t = date.new(base_leap_date) -- :sub{table}
    local tsub_i = date.new(base_leap_date) -- :sub{interval}
    local tminus_i = date.new(base_leap_date) -- t - interval
    local tminus_t = date.new(base_leap_date) -- t - {table}

    -- check subtractions
    for _, row in pairs(leap_ivals_check) do
        local delta, _, ssub = unpack(row)
        -- t:add{table}
        test:is(tostring(tsub_t:sub(delta)), ssub,
                ('sub delta to %s'):format(ssub))
        -- t:add(interval)
        test:is(tostring(tsub_i:sub(ival_new(delta))), ssub,
                ('sub ival delta to %s'):format(ssub))
        -- t + interval
        tminus_i = tminus_i - ival_new(delta)
        test:is(tostring(tminus_i), ssub, ('%s: t - ival delta'):format(ssub))
        -- t - interval
        tminus_t = tminus_t - delta
        test:is(tostring(tminus_t), ssub, ('%s: t - table delta'):format(ssub))
    end

    -- check average, not leap dates
    local base_non_leap_date = {year = 1970, month = 1, day = 8}
    local non_leap_ivals_check = {
        {{year = 1, month = 2}, '1971-03-08T00:00:00Z', '1968-11-08T00:00:00Z'},
        {{week = 10}, '1971-05-17T00:00:00Z', '1968-08-30T00:00:00Z'},
        {{day = 15}, '1971-06-01T00:00:00Z', '1968-08-15T00:00:00Z'},
        {{hour = 2}, '1971-06-01T02:00:00Z', '1968-08-14T22:00:00Z'},
        {{min = 15}, '1971-06-01T02:15:00Z', '1968-08-14T21:45:00Z'},
        {{sec = 48}, '1971-06-01T02:15:48Z', '1968-08-14T21:44:12Z'},
        {{nsec = 2e9}, '1971-06-01T02:15:50Z', '1968-08-14T21:44:10Z'},
        {{hour = 12, min = 600, sec = 1024}, '1971-06-02T00:32:54Z',
         '1968-08-13T23:27:06Z'},
    }

    tadd_t = date.new(base_non_leap_date) -- :add{table}
    tadd_i = date.new(base_non_leap_date) -- :add{interval}
    tplus_i = date.new(base_non_leap_date) -- t + interval
    tplus_t = date.new(base_non_leap_date) -- t + {table}

    -- check additions
    for _, row in pairs(non_leap_ivals_check) do
        local delta, sadd, _ = unpack(row)
        -- t:add{table}
        test:is(tostring(tadd_t:add(delta)), sadd,
                ('add delta to %s'):format(sadd))
        -- t:add(interval)
        test:is(tostring(tadd_i:add(ival_new(delta))), sadd,
                ('add ival delta to %s'):format(sadd))
        -- t + interval
        tplus_i = tplus_i + ival_new(delta)
        test:is(tostring(tplus_i), sadd, ('%s: t + ival delta'):format(sadd))
        -- t - interval
        tplus_t = tplus_t + delta
        test:is(tostring(tplus_t), sadd, ('%s: t + table delta'):format(sadd))
    end

    tsub_t = date.new(base_non_leap_date) -- :sub{table}
    tsub_i = date.new(base_non_leap_date) -- :sub{interval}
    tminus_i = date.new(base_non_leap_date) -- t - interval
    tminus_t = date.new(base_non_leap_date) -- t - {table}

    -- check subtractions
    for _, row in pairs(non_leap_ivals_check) do
        local delta, _, ssub = unpack(row)
        -- t:add{table}
        test:is(tostring(tsub_t:sub(delta)), ssub,
                ('sub delta to %s'):format(ssub))
        -- t:add(interval)
        test:is(tostring(tsub_i:sub(ival_new(delta))), ssub,
                ('sub ival delta to %s'):format(ssub))
        -- t + interval
        tminus_i = tminus_i - ival_new(delta)
        test:is(tostring(tminus_i), ssub, ('%s: t - ival delta'):format(ssub))
        -- t - interval
        tminus_t = tminus_t - delta
        test:is(tostring(tminus_t), ssub, ('%s: t - table delta'):format(ssub))
    end

    -- check nsec boundary overflow
    local i1, i2
    i1 = ival_new{nsec = 1e9 - 1}
    i2 = ival_new{nsec = 2}
    test:is(tostring(i1 + i2), '+1.000000001 seconds', 'nsec: 999999999 + 2')
    i1 = ival_new{nsec = -1e9 + 1}
    i2 = ival_new{nsec = -2}
    test:is(tostring(i1 + i2), '-1.000000001 seconds', 'nsec: -999999999 - 2')

    local specific_errors = {
        {only_one_of, { nsec = 123456, usec = 123}},
        {only_one_of, { nsec = 123456, msec = 123}},
        {only_one_of, { usec = 123, msec = 123}},
        {only_one_of, { nsec = 123456, usec = 123, msec = 123}},
        {int_ival_exp, { sec = 12345.125, usec = 123}},
        {int_ival_exp, { sec = 12345.125, msec = 123}},
        {int_ival_exp, { sec = 12345.125, nsec = 123}},
    }
    for _, row in pairs(specific_errors) do
        local err_msg, obj = unpack(row)
        assert_raises(test, err_msg, function() return tadd_t:add(obj) end)
        assert_raises(test, err_msg, function() return tsub_t:sub(obj) end)
    end
    assert_raises_like(test, expected_interval_but, function() tadd_t:add('bogus') end)
    assert_raises_like(test, expected_interval_but, function() tadd_t:add(123) end)
    assert_raises_like(test, expected_interval_but, function() tsub_t:sub('bogus') end)
    assert_raises_like(test, expected_interval_but, function() tsub_t:sub(123) end)
end)

test:test("Time interval operations - determinism", function(test)
    test:plan(4)
    local d1 = date.new()
    local d2 = date.new()
    local negsec0 = d1 - d2
    local possec0 = d2 - d1
    test:is(negsec0.sec, 0, "date.now() - date.now() should be 0")
    test:is(possec0.sec, 0, "date.now() - date.now() should be 0 in reverse")

    d1 = date.new{year = 2021, mon = 1, day = 12, hour = 12, min = 10, sec = 10,
                  nsec = 4e6}
    d2 = date.new{year = 2021, mon = 1, day = 12, hour = 12, min = 10, sec = 10,
                  nsec = 5e6}
    negsec0 = d1 - d2
    possec0 = d2 - d1
    test:is(negsec0.sec, 0, "nsec = 4e6 - 5e6 should be 0")
    test:is(possec0.sec, 0, "nsec = 5e6 - 4e6 should be 0")
    end)

test:test("Time interval operations - different adjustments", function(test)
    test:plan(60)

    -- check arithmetic with leap dates
    local base_leap_date = { year = 1972, month = 2, day = 29}
    local adj_modes = {
        [1] = "last", -- default if omitted
        [2] = "none",
        [3] = "excess",
    }

    local leap_ivals_check_add = {
        {{month = 1}, {'1972-03-31T00:00:00Z', '1972-03-29T00:00:00Z',
                       '1972-03-29T00:00:00Z'}},
        {{month = 1}, {'1972-04-30T00:00:00Z', '1972-04-29T00:00:00Z',
                       '1972-04-29T00:00:00Z'}},
        {{month = 5}, {'1972-09-30T00:00:00Z', '1972-09-29T00:00:00Z',
                       '1972-09-29T00:00:00Z'}},
        {{year = 1, month = 2}, {'1973-11-30T00:00:00Z', '1973-11-29T00:00:00Z',
                                 '1973-11-29T00:00:00Z'}},
        {{year = 2, month = 3}, {'1976-02-29T00:00:00Z', '1976-02-29T00:00:00Z',
                                 '1976-02-29T00:00:00Z'}},
        {{month = 1}, {'1976-03-31T00:00:00Z', '1976-03-29T00:00:00Z',
                       '1976-03-29T00:00:00Z'}},
        {{month = 1}, {'1976-04-30T00:00:00Z', '1976-04-29T00:00:00Z',
                       '1976-04-29T00:00:00Z'}},
        {{month = 5}, {'1976-09-30T00:00:00Z', '1976-09-29T00:00:00Z',
                       '1976-09-29T00:00:00Z'}},
        {{year = -1}, {'1975-09-30T00:00:00Z', '1975-09-29T00:00:00Z',
                       '1975-09-29T00:00:00Z'}},
        {{year = -1}, {'1974-09-30T00:00:00Z', '1974-09-29T00:00:00Z',
                       '1974-09-29T00:00:00Z'}},
    }

    local tadd = {}
    for i=1,3 do
        tadd[i] = date.new(base_leap_date)
    end

    -- check additions with adjustments
    for _, row in pairs(leap_ivals_check_add) do
        local delta, sadds = unpack(row)
        for i=1,3 do
            local tsbefore = tostring(tadd[i])
            local adjust = adj_modes[i]
            local sadd = sadds[i]
            delta.adjust = adjust
            test:is(tostring(tadd[i]:add(delta)), sadd,
                    ('add delta to %s with adjust %s'):format(tsbefore, adjust))

        end
    end

    local leap_ivals_check_sub = {
        {{month = 1}, {'1972-01-31T00:00:00Z', '1972-01-29T00:00:00Z',
                       '1972-01-29T00:00:00Z'}},
        {{month = 1}, {'1971-12-31T00:00:00Z', '1971-12-29T00:00:00Z',
                       '1971-12-29T00:00:00Z'}},
        {{month = 5}, {'1971-07-31T00:00:00Z', '1971-07-29T00:00:00Z',
                       '1971-07-29T00:00:00Z'}},
        {{year = 1, month = 2}, {'1970-05-31T00:00:00Z', '1970-05-29T00:00:00Z',
                       '1970-05-29T00:00:00Z'}},
        {{year = 2, month = 3}, {'1968-02-29T00:00:00Z', '1968-02-29T00:00:00Z',
                       '1968-02-29T00:00:00Z'}},
        {{month = 1}, {'1968-01-31T00:00:00Z', '1968-01-29T00:00:00Z',
                       '1968-01-29T00:00:00Z'}},
        {{month = 1}, {'1967-12-31T00:00:00Z', '1967-12-29T00:00:00Z',
                       '1967-12-29T00:00:00Z'}},
        {{month = 5}, {'1967-07-31T00:00:00Z', '1967-07-29T00:00:00Z',
                       '1967-07-29T00:00:00Z'}},
        {{year = -1}, {'1968-07-31T00:00:00Z', '1968-07-29T00:00:00Z',
                       '1968-07-29T00:00:00Z'}},
        {{year = -1}, {'1969-07-31T00:00:00Z', '1969-07-29T00:00:00Z',
                       '1969-07-29T00:00:00Z'}},
    }

    local tsub = {}
    for i=1,3 do
        tsub[i] = date.new(base_leap_date)
    end

    -- check subtractions with adjustments
    for _, row in pairs(leap_ivals_check_sub) do
        local delta, ssubs = unpack(row)
        for i=1,3 do
            local tsbefore = tostring(tsub[i])
            local adjust = adj_modes[i]
            local ssub = ssubs[i]
            delta.adjust = adjust
            test:is(tostring(tsub[i]:sub(delta)), ssub,
                    ('sub delta to %s with adjust %s'):format(tsbefore, adjust))
        end
    end
end)

test:test("Time intervals creation - range checks", function(test)
    test:plan(23)

    local inew = date.interval.new

    local specific_errors = {
        {only_one_of, { nsec = 123456, usec = 123}},
        {only_one_of, { nsec = 123456, msec = 123}},
        {only_one_of, { usec = 123, msec = 123}},
        {only_one_of, { nsec = 123456, usec = 123, msec = 123}},
        {int_ival_exp, { sec = 12345.125, usec = 123}},
        {int_ival_exp, { sec = 12345.125, msec = 123}},
        {int_ival_exp, { sec = 12345.125, nsec = 123}},
        {table_expected('interval.new()', '2001-01-01'), '2001-01-01'},
        {table_expected('interval.new()', 20010101), 20010101},
        {range_check_error('year', 1e21, {-MAX_YEAR_RANGE, MAX_YEAR_RANGE}),
            {year = 1e21}},
        {range_check_error('year', -1e21, {-MAX_YEAR_RANGE, MAX_YEAR_RANGE}),
            {year = -1e21}},
        {range_check_error('month', 1e21, {-MAX_MONTH_RANGE, MAX_MONTH_RANGE}),
            {month = 1e21}},
        {range_check_error('month', -1e21, {-MAX_MONTH_RANGE, MAX_MONTH_RANGE}),
            {month = -1e21}},
        {range_check_error('week', 1e21, {-MAX_WEEK_RANGE, MAX_WEEK_RANGE}),
            {week = 1e21}},
        {range_check_error('week', -1e21, {-MAX_WEEK_RANGE, MAX_WEEK_RANGE}),
            {week = -1e21}},
        {range_check_error('day', 1e21, {-MAX_DAY_RANGE, MAX_DAY_RANGE}),
            {day = 1e21}},
        {range_check_error('day', -1e21, {-MAX_DAY_RANGE, MAX_DAY_RANGE}),
            {day = -1e21}},
        {range_check_error('hour', 1e21, {-MAX_HOUR_RANGE, MAX_HOUR_RANGE}),
            {hour = 1e21}},
        {range_check_error('hour', -1e21, {-MAX_HOUR_RANGE, MAX_HOUR_RANGE}),
            {hour = -1e21}},
        {range_check_error('min', 1e21, {-MAX_MIN_RANGE, MAX_MIN_RANGE}),
            {min = 1e21}},
        {range_check_error('min', -1e21, {-MAX_MIN_RANGE, MAX_MIN_RANGE}),
            {min = -1e21}},
        {range_check_error('sec', 1e21, {-MAX_SEC_RANGE, MAX_SEC_RANGE}),
            {sec = 1e21}},
        {range_check_error('sec', -1e21, {-MAX_SEC_RANGE, MAX_SEC_RANGE}),
            {sec = -1e21}},
    }
    for _, row in pairs(specific_errors) do
        local err_msg, attribs = unpack(row)
        assert_raises(test, err_msg, function() return inew(attribs) end)
    end

end)

test:test("Months intervals with last days", function(test)
    test:plan(122)
    local month1 = date.interval.new{month = 1, adjust = 'last'}
    local base_day = date.new{year = 1998, month = 12, day = 31}
    local current = base_day
    test:is(current, date.new{year = 1998, month = 12, day = -1}, "day = -1")

    -- check the fact that last day of month will snap
    -- forward moves by 1 month
    for year=1999,2003 do
        for month=1,12 do
            local month_last_day = date.new{year = year, month = month, day = -1}
            current = current + month1
            test:is(month_last_day, current, ('%s: last day of %d/%d'):
                    format(current, year, month))
        end
    end

    -- backward moves by 1 month
    current = date.new{year = 2004, month = 1, day = 31}
    test:is(current, date.new{year = 2004, month = 1, day = -1}, "day #2 = -1")

    for year=2003,1999,-1 do
        for month=12,1,-1 do
            local month_last_day = date.new{year = year, month = month, day = -1}
            current = current - month1
            test:is(month_last_day, current, ('%s: last day of %d/%d'):
                    format(current, year, month))
        end
    end
end)

local function catchadd(A, B)
    return pcall(function() return A + B end)
end

--[[
Matrix of addition operands eligibility and their result type

|                 | datetime | interval | table    |
+-----------------+----------+----------+----------+
| datetime        |          | datetime | datetime |
| interval        | datetime | interval | interval |
| table           |          |          |          |
]]
test:test("Matrix of allowed time and interval additions", function(test)
    test:plan(67)

    -- check arithmetic with leap dates
    local T1970 = date.new{year = 1970, month = 1, day = 1}
    local T2000 = date.new{year = 2000, month = 1, day = 1}

    local I1 = date.interval.new{day = 1}
    local M2 = date.interval.new{month = 2}
    local M10 = date.interval.new{month = 10}
    local Y1 = date.interval.new{year = 1}
    local Y5 = date.interval.new{year = 5}

    local i1 = {day = 1}
    local m2 = {month = 2}
    local m10 = {month = 10}
    local y1 = {year = 1}
    local y5 = {year = 5}

    test:is(catchadd(T1970, I1), true, "status: T + I")
    test:is(catchadd(T1970, i1), true, "status: T + i")
    test:is(catchadd(T1970, M2), true, "status: T + M")
    test:is(catchadd(T1970, m2), true, "status: T + m")
    test:is(catchadd(T1970, Y1), true, "status: T + Y")
    test:is(catchadd(T1970, y1), true, "status: T + y")
    test:is(catchadd(T1970, T2000), false, "status: T + T")
    test:is(catchadd(I1, T1970), true, "status: I + T")
    test:is(catchadd(i1, T1970), true, "status: i + T")
    test:is(catchadd(M2, T1970), true, "status: M + T")
    test:is(catchadd(m2, T1970), true, "status: m + T")
    test:is(catchadd(Y1, T1970), true, "status: Y + T")
    test:is(catchadd(y1, T1970), true, "status: y + T")
    test:is(catchadd(I1, Y1), true, "status: I + Y")
    test:is(catchadd(I1, y1), true, "status: I + y")
    test:is(catchadd(i1, y1), false, "status: i + y")
    test:is(catchadd(i1, Y1), false, "status: i + Y")
    test:is(catchadd(M2, Y1), true, "status: M + Y")
    test:is(catchadd(M2, y1), true, "status: M + y")
    test:is(catchadd(m2, y1), false, "status: m + y")
    test:is(catchadd(m2, Y1), false, "status: m + Y")
    test:is(catchadd(I1, Y1), true, "status: I + Y")
    test:is(catchadd(I1, y1), true, "status: I + y")
    test:is(catchadd(i1, y1), false, "status: i + y")
    test:is(catchadd(i1, Y1), false, "status: i + Y")
    test:is(catchadd(Y5, M10), true, "status: Y + M")
    test:is(catchadd(Y5, m10), true, "status: Y + m")
    test:is(catchadd(y5, m10), false, "status: y + m")
    test:is(catchadd(y5, M10), false, "status: y + M")
    test:is(catchadd(Y5, I1), true, "status: Y + I")
    test:is(catchadd(Y5, i1), true, "status: Y + i")
    test:is(catchadd(y5, i1), false, "status: y + i")
    test:is(catchadd(y5, I1), false, "status: y + I")
    test:is(catchadd(Y5, Y1), true, "status: Y + Y")
    test:is(catchadd(Y5, y1), true, "status: Y + y")
    test:is(catchadd(y5, y1), false, "status: y + y")
    test:is(catchadd(y5, Y1), false, "status: y + Y")

    test:is(tostring(T1970 + I1), "1970-01-02T00:00:00Z", "value: T + I")
    test:is(tostring(T1970 + i1), "1970-01-02T00:00:00Z", "value: T + i")
    test:is(tostring(T1970 + M2), "1970-03-01T00:00:00Z", "value: T + M")
    test:is(tostring(T1970 + m2), "1970-03-01T00:00:00Z", "value: T + m")
    test:is(tostring(T1970 + Y1), "1971-01-01T00:00:00Z", "value: T + Y")
    test:is(tostring(T1970 + y1), "1971-01-01T00:00:00Z", "value: T + y")
    test:is(tostring(I1 + T1970), "1970-01-02T00:00:00Z", "value: I + T")
    test:is(tostring(M2 + T1970), "1970-03-01T00:00:00Z", "value: M + T")
    test:is(tostring(Y1 + T1970), "1971-01-01T00:00:00Z", "value: Y + T")
    test:is(tostring(Y5 + Y1), "+6 years", "Y + Y")

    assert_raises_like(test, expected_interval_but,
                       function() return T1970 + 123 end)
    assert_raises_like(test, expected_interval_but,
                       function() return T1970 + "0" end)

    local max_date = date.new{year = MAX_DATE_YEAR - 1}
    local new_ival = date.interval.new
    test:is(catchadd(max_date, new_ival{year = 1}), true, "max + 1y")
    test:is(catchadd(max_date, new_ival{year = 2}), false, "max + 2y")
    test:is(catchadd(max_date, new_ival{year = 10}), false, "max + 10y")
    test:is(catchadd(max_date, new_ival{month = 1}), true, "max + 1m")
    test:is(catchadd(max_date, new_ival{month = 6}), true, "max + 6m")
    test:is(catchadd(max_date, new_ival{month = 12}), true, "max + 12m")
    test:is(catchadd(max_date, new_ival{month = 24}), false, "max + 24m")
    test:is(catchadd(max_date, new_ival{week = 10}), true, "max + 10wk")
    test:is(catchadd(max_date, new_ival{week = 100}), false, "max + 100wk")

    test:is(catchadd(max_date, {year = 1}), true, "max + 1y")
    test:is(catchadd(max_date, {year = 2}), false, "max + 2y")
    test:is(catchadd(max_date, {year = 10}), false, "max + 10y")
    test:is(catchadd(max_date, {month = 1}), true, "max + 1m")
    test:is(catchadd(max_date, {month = 6}), true, "max + 6m")
    test:is(catchadd(max_date, {month = 12}), true, "max + 12m")
    test:is(catchadd(max_date, {month = 24}), false, "max + 24m")
    test:is(catchadd(max_date, {week = 10}), true, "max + 10wk")
    test:is(catchadd(max_date, {week = 100}), false, "max + 100wk")

end)

local function catchsub_status(A, B)
    return pcall(function() return A - B end)
end

--[[
Matrix of subtraction operands eligibility and their result type

|                 | datetime | interval | table    |
+-----------------+----------+----------+----------+
| datetime        | interval | datetime | datetime |
| interval        |          | interval | interval |
| table           |          |          |          |
]]
test:test("Matrix of allowed time and interval subtractions", function(test)
    test:plan(29)

    -- check arithmetic with leap dates
    local T1970 = date.new{year = 1970, month = 1, day = 1}
    local T2000 = date.new{year = 2000, month = 1, day = 1}
    local I1 = date.interval.new{day = 1}
    local M2 = date.interval.new{month = 2}
    local M10 = date.interval.new{month = 10}
    local Y1 = date.interval.new{year = 1}
    local Y5 = date.interval.new{year = 5}

    test:is(catchsub_status(T1970, I1), true, "status: T - I")
    test:is(catchsub_status(T1970, M2), true, "status: T - M")
    test:is(catchsub_status(T1970, Y1), true, "status: T - Y")
    test:is(catchsub_status(T1970, T2000), true, "status: T - T")
    test:is(catchsub_status(I1, T1970), false, "status: I - T")
    test:is(catchsub_status(M2, T1970), false, "status: M - T")
    test:is(catchsub_status(Y1, T1970), false, "status: Y - T")
    test:is(catchsub_status(I1, Y1), true, "status: I - Y")
    test:is(catchsub_status(M2, Y1), true, "status: M - Y")
    test:is(catchsub_status(I1, Y1), true, "status: I - Y")
    test:is(catchsub_status(Y5, M10), true, "status: Y - M")
    test:is(catchsub_status(Y5, I1), true, "status: Y - I")
    test:is(catchsub_status(Y5, Y1), true, "status: Y - Y")

    test:is(tostring(T1970 - I1), "1969-12-31T00:00:00Z", "value: T - I")
    test:is(tostring(T1970 - M2), "1969-11-01T00:00:00Z", "value: T - M")
    test:is(tostring(T1970 - Y1), "1969-01-01T00:00:00Z", "value: T - Y")
    test:is(tostring(T1970 - T2000), "-10957 days, 0 hours, 0 minutes, 0 seconds",
            "value: T - T")
    test:is(tostring(Y5 - Y1), "+4 years", "value: Y - Y")

    assert_raises_like(test, expected_datetime_but,
                       function() return T1970 - 123 end)
    assert_raises_like(test, expected_datetime_but,
                       function() return T1970 - "0" end)

    local min_date = date.new{year = MIN_DATE_YEAR + 1}
    local new_ival = date.interval.new
    test:is(catchsub_status(min_date, new_ival{year = 1}), false, "min - 1y")
    test:is(catchsub_status(min_date, new_ival{year = 2}), false, "min - 2y")
    test:is(catchsub_status(min_date, new_ival{year = 10}), false, "min - 10y")
    test:is(catchsub_status(min_date, new_ival{month = 1}), true, "min - 1m")
    test:is(catchsub_status(min_date, new_ival{month = 6}), true, "min - 6m")
    test:is(catchsub_status(min_date, new_ival{month = 12}), false, "min - 12m")
    test:is(catchsub_status(min_date, new_ival{month = 24}), false, "min - 24m")
    test:is(catchsub_status(min_date, new_ival{week = 10}), true, "min - 10wk")
    test:is(catchsub_status(min_date, new_ival{week = 100}), false, "min - 100wk")
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
