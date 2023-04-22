#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('errno')
local date = require('datetime')
local ffi = require('ffi')
local TZ = date.TZ

--[[
    Workaround for #6599 where we may randomly fail on AWS Graviton machines,
    while it's working properly when jit disabled.
--]]
if jit.arch == 'arm64' then
    jit.off()
    jit.flush()
end

test:plan(39)

-- minimum supported date - -5879610-06-22
local MIN_DATE_YEAR = -5879610
-- maximum supported date - 5879611-07-11
local MAX_DATE_YEAR = 5879611

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

local function ival_overflow(op, name, value, max)
    return ('%s moves value %s of %s out of allowed range [%s, %s]'):
            format(op, value, name, -max, max)
end

local function invalid_date(y, M, d)
    return ('date %d-%02d-%02d is invalid'):format(y, M, d)
end

local function invalid_tz_fmt_error(val)
    return ('invalid time-zone format %s'):format(val)
end

local function invalid_date_fmt_error(str)
    return ('invalid date format %s'):format(str)
end

local function assert_raises(test, error_msg, func, ...)
    local ok, err = pcall(func, ...)
    local err_tail = err and err:gsub("^.+:%d+: ", "") or ''
    return test:is(not ok and err_tail, error_msg,
                   ('"%s" received, "%s" expected'):format(err_tail, error_msg))
end

local function assert_raises_like(test, error_msg, func, ...)
    local ok, err = pcall(func, ...)
    local err_tail = err and err:gsub("^.+:%d+: ", "") or ''
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
    test:plan(14)
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
        { 'tz', 'MSK', '2000-11-30T06:12:23 MSK' },
        { 'tz', 'Z', '2000-11-30T06:12:23Z' },
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
    test:plan(83)

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
        {table_expected('datetime.new()', '2001-01-01'), '2001-01-01'},
        {table_expected('datetime.new()', 20010101), 20010101},
        {range_check_3_error('day', 32, {-1, 1, 31}),
            {year = 2021, month = 6, day = 32}},
        {invalid_days_in_mon(31, 6, 2021), { year = 2021, month = 6, day = 31}},
        {invalid_date(-5879610, 6, 21),
            {year = -5879610, month = 6, day = 21}},
        {invalid_date(-5879610, 1, 1),
            {year = -5879610, month = 1, day = 1}},
        {range_check_error('year', -16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = -16009610, month = 12, day = 31}},
        {range_check_error('year', 16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = 16009610, month = 1, day = 1}},
        {invalid_date(MAX_DATE_YEAR, 9, 1),
            {year = MAX_DATE_YEAR, month = 9, day = 1}},
        {invalid_date(MAX_DATE_YEAR, 7, 12),
            {year = MAX_DATE_YEAR, month = 7, day = 12}},
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

test:test("Simple tests for parser", function(test)
    test:plan(12)
    test:ok(date.parse("1970-01-01T01:00:00Z") ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0})
    test:ok(date.parse("1970-01-01T01:00:00Z", {format = 'iso8601'}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0})
    test:ok(date.parse("1970-01-01T01:00:00Z", {format = 'rfc3339'}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0})
    test:ok(date.parse("2020-01-01T01:00:00+00:00", {format = 'rfc3339'}) ==
            date.parse("2020-01-01T01:00:00+00:00", {format = 'iso8601'}))
    test:ok(date.parse("1970-01-01T02:00:00+02:00") ==
            date.new{year=1970, mon=1, day=1, hour=2, min=0, sec=0, tzoffset=120})
    test:ok(date.parse("1970-01-01T01:00:00", {tzoffset = 120}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0, tzoffset=120})
    test:ok(date.parse("1970-01-01T01:00:00", {tzoffset = '+0200'}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0, tzoffset=120})
    test:ok(date.parse("1970-01-01T01:00:00", {tzoffset = '+02:00'}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0, tzoffset=120})
    test:ok(date.parse("1970-01-01T01:00:00Z", {tzoffset = '+02:00'}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0, tzoffset=0})
    test:ok(date.parse("1970-01-01T01:00:00+01:00", {tzoffset = '+02:00'}) ==
            date.new{year=1970, mon=1, day=1, hour=1, min=0, sec=0, tzoffset=60})

    test:ok(date.parse("1970-01-01T02:00:00Z") <
            date.new{year=1970, mon=1, day=1, hour=2, min=0, sec=1})
    test:ok(date.parse("1970-01-01T02:00:00Z") <=
            date.new{year=1970, mon=1, day=1, hour=2, min=0, sec=0})
end)

test:test("Multiple tests for parser (with nanoseconds)", function(test)
    test:plan(211)
    -- borrowed from
    -- github.com/chansen/p5-time-moment/blob/master/t/180_from_string.t
    local tests =
    {
        --{ iso-8601 string, epoch, nanoseconds, tz-offset, do reverse check?}
        {'0001-01-01T00:00:00Z',    -62135596800,         0,    0, 0},
        {'00010101T000000Z',        -62135596800,         0,    0, 0},
        {'0001-W01-1T00:00:00Z',    -62135596800,         0,    0, 0},
        {'0001W011T000000Z',        -62135596800,         0,    0, 0},
        {'0001-001T00:00:00Z',      -62135596800,         0,    0, 0},
        {'0001001T000000Z',         -62135596800,         0,    0, 0},
        {'1970-01-01T00:00:00Z',               0,         0,    0, 1},
        {'1970-01-01T02:00:00+0200',           0,         0,  120, 1},
        {'1970-01-01T01:30:00+0130',           0,         0,   90, 1},
        {'1970-01-01T01:00:00+0100',           0,         0,   60, 1},
        {'1970-01-01T00:01:00+0001',           0,         0,    1, 1},
        {'1970-01-01T00:00:00Z',               0,         0,    0, 1},
        {'1969-12-31T23:59:00-0001',           0,         0,   -1, 1},
        {'1969-12-31T23:00:00-0100',           0,         0,  -60, 1},
        {'1969-12-31T22:30:00-0130',           0,         0,  -90, 1},
        {'1969-12-31T22:00:00-0200',           0,         0, -120, 1},
        {'1970-01-01T00:00:00.123456789Z',     0, 123456789,    0, 1},
        {'1970-01-01T00:00:00.12345678Z',      0, 123456780,    0, 0},
        {'1970-01-01T00:00:00.1234567Z',       0, 123456700,    0, 0},
        {'1970-01-01T00:00:00.123456Z',        0, 123456000,    0, 1},
        {'1970-01-01T00:00:00.12345Z',         0, 123450000,    0, 0},
        {'1970-01-01T00:00:00.1234Z',          0, 123400000,    0, 0},
        {'1970-01-01T00:00:00.123Z',           0, 123000000,    0, 1},
        {'1970-01-01T00:00:00.12Z',            0, 120000000,    0, 0},
        {'1970-01-01T00:00:00.1Z',             0, 100000000,    0, 0},
        {'1970-01-01T00:00:00.01Z',            0,  10000000,    0, 0},
        {'1970-01-01T00:00:00.001Z',           0,   1000000,    0, 1},
        {'1970-01-01T00:00:00.0001Z',          0,    100000,    0, 0},
        {'1970-01-01T00:00:00.00001Z',         0,     10000,    0, 0},
        {'1970-01-01T00:00:00.000001Z',        0,      1000,    0, 1},
        {'1970-01-01T00:00:00.0000001Z',       0,       100,    0, 0},
        {'1970-01-01T00:00:00.00000001Z',      0,        10,    0, 0},
        {'1970-01-01T00:00:00.000000001Z',     0,         1,    0, 1},
        {'1970-01-01T00:00:00.000000009Z',     0,         9,    0, 1},
        {'1970-01-01T00:00:00.00000009Z',      0,        90,    0, 0},
        {'1970-01-01T00:00:00.0000009Z',       0,       900,    0, 0},
        {'1970-01-01T00:00:00.000009Z',        0,      9000,    0, 1},
        {'1970-01-01T00:00:00.00009Z',         0,     90000,    0, 0},
        {'1970-01-01T00:00:00.0009Z',          0,    900000,    0, 0},
        {'1970-01-01T00:00:00.009Z',           0,   9000000,    0, 1},
        {'1970-01-01T00:00:00.09Z',            0,  90000000,    0, 0},
        {'1970-01-01T00:00:00.9Z',             0, 900000000,    0, 0},
        {'1970-01-01T00:00:00.99Z',            0, 990000000,    0, 0},
        {'1970-01-01T00:00:00.999Z',           0, 999000000,    0, 1},
        {'1970-01-01T00:00:00.9999Z',          0, 999900000,    0, 0},
        {'1970-01-01T00:00:00.99999Z',         0, 999990000,    0, 0},
        {'1970-01-01T00:00:00.999999Z',        0, 999999000,    0, 1},
        {'1970-01-01T00:00:00.9999999Z',       0, 999999900,    0, 0},
        {'1970-01-01T00:00:00.99999999Z',      0, 999999990,    0, 0},
        {'1970-01-01T00:00:00.999999999Z',     0, 999999999,    0, 1},
        {'1970-01-01T00:00:00.0Z',             0,         0,    0, 0},
        {'1970-01-01T00:00:00.00Z',            0,         0,    0, 0},
        {'1970-01-01T00:00:00.000Z',           0,         0,    0, 0},
        {'1970-01-01T00:00:00.0000Z',          0,         0,    0, 0},
        {'1970-01-01T00:00:00.00000Z',         0,         0,    0, 0},
        {'1970-01-01T00:00:00.000000Z',        0,         0,    0, 0},
        {'1970-01-01T00:00:00.0000000Z',       0,         0,    0, 0},
        {'1970-01-01T00:00:00.00000000Z',      0,         0,    0, 0},
        {'1970-01-01T00:00:00.000000000Z',     0,         0,    0, 0},
        {'1973-11-29T21:33:09Z',       123456789,         0,    0, 1},
        {'2013-10-28T17:51:56Z',      1382982716,         0,    0, 1},
        {'9999-12-31T23:59:59Z',    253402300799,         0,    0, 1},
    }
    for _, value in ipairs(tests) do
        local str, epoch, nsec, tzoffset, check
        str, epoch, nsec, tzoffset, check = unpack(value)
        local dt = date.parse(str)
        test:is(dt.epoch, epoch, ('%s: dt.epoch == %d'):format(str, epoch))
        test:is(dt.nsec, nsec, ('%s: dt.nsec == %d'):format(str, nsec))
        test:is(dt.tzoffset, tzoffset, ('%s: dt.tzoffset == %d'):format(str, tzoffset))
        if check > 0 then
            test:is(str, tostring(dt), ('%s == tostring(%s)'):
                    format(str, tostring(dt)))
        end
    end
end)

local function create_date_string(date)
    local year, month, day = date.year or 1970, date.month or 1, date.day or 1
    local hour, min, sec = date.hour or 0, date.min or 0, date.sec or 0
    return ('%04d-%02d-%02dT%02d:%02d:%02dZ'):format(year, month, day, hour, min, sec)
end

test:test("Check parsing of full supported years range", function(test)
    test:plan(63)
    local valid_years = {
        -5879610, -5879000, -5800000, -2e6, -1e5, -1e4, -9999, -2000, -1000,
        0, 1, 1000, 1900, 1970, 2000, 9999,
        1e4, 1e6, 2e6, 5e6, 5879611
    }
    local fmt = '%FT%T%z'
    for _, y in ipairs(valid_years) do
        local txt = ('%04d-06-22'):format(y)
        local dt = date.parse(txt)
        test:isnt(dt, nil, dt)
        local out_txt = tostring(dt)
        local out_dt = date.parse(out_txt)
        test:is(dt, out_dt, ('default parse of %s (%s == %s)'):
                            format(out_txt, dt, out_dt))
        local fmt_dt = date.parse(out_txt, {format = fmt})
        test:is(dt, fmt_dt, ('parse via format %s (%s == %s)'):
                            format(fmt, dt, fmt_dt))
    end
end)

local function couldnt_parse(txt)
    return ("could not parse '%s'"):format(txt)
end

test:test("Check parsing of dates with invalid attributes", function(test)
    test:plan(32)

    local boundary_checks = {
        {'month', {1, 12}},
        {'day', {1, 31, -1}},
        {'hour', {0, 23}},
        {'min', {0, 59}},
        {'sec', {0, 59}},
    }
    for _, row in pairs(boundary_checks) do
        local attr_name, bounds = unpack(row)
        local left, right = unpack(bounds)
        local txt = create_date_string{[attr_name] = left}
        local dt, len = date.parse(txt)
        test:ok(dt ~= nil, dt)
        test:ok(len == #txt, len)
        local txt = create_date_string{[attr_name] = right}
        dt, len = date.parse(txt)
        test:ok(dt ~= nil, dt)
        test:ok(len == #txt, len)
        -- expected error
        if left > 0 then
            txt = create_date_string{[attr_name] = left - 1}
            assert_raises(test, couldnt_parse(txt),
                          function() dt, len = date.parse(txt) end)
        end
        txt = create_date_string{[attr_name] = right + 2}
        assert_raises(test, couldnt_parse(txt),
                      function() dt, len = date.parse(txt) end)
        txt = create_date_string{[attr_name] = right + 50}
        assert_raises(test, couldnt_parse(txt),
                      function() dt, len = date.parse(txt) end)
    end
end)

test:test("Parsing of timezone abbrevs", function(test)
    test:plan(220)
    local zone_abbrevs = {
        -- military
        A =   1*60, B =   2*60, C =   3*60,
        D =   4*60, E =   5*60, F =   6*60,
        G =   7*60, H =   8*60, I =   9*60,
        K =  10*60, L =  11*60, M =  12*60,

        N =  -1*60, O =  -2*60, P =  -3*60,
        Q =  -4*60, R =  -5*60, S =  -6*60,
        T =  -7*60, U =  -8*60, V =  -9*60,
        W = -10*60, X = -11*60, Y = -12*60,

        Z = 0,

        -- universal
        GMT = 0, UTC = 0, UT = 0,
        -- some non ambiguous
        MSK = 3 * 60,   MCK = 3 * 60,   CET = 1 * 60,
        AMDT = 5 * 60,  BDST = 1 * 60,  IRKT = 8 * 60,
        KST = 9 * 60,   PDT = -7 * 60,  WET = 0 * 60,
        HOVDST = 8 * 60, CHODST = 9 * 60,

        -- Olson
        ['Europe/Moscow'] = 180,
        ['Africa/Abidjan'] = 0,
        ['America/Argentina/Buenos_Aires'] = -180,
        ['Asia/Krasnoyarsk'] = 420,
        ['Pacific/Fiji'] = 720,
    }
    local exp_pattern = '^2020%-02%-10T00:00'
    local base_date = '2020-02-10T0000 '

    for zone, offset in pairs(zone_abbrevs) do
        local date_text = base_date .. zone
        local date, len = date.parse(date_text)
        test:isnt(date, nil, 'parse ' .. zone)
        test:ok(len > #base_date, 'length longer than ' .. #base_date)
        test:is(1, tostring(date):find(exp_pattern), 'expected prefix')
        test:is(date.tzoffset, offset, 'expected offset')
        test:is(date.tz, zone, 'expected timezone name')
    end
end)

test:test("Parsing of timezone names (tzindex)", function(test)
    test:plan(396)
    local zone_abbrevs = {
        -- military
        A =  1, B =  2, C =  3,
        D =  4, E =  5, F =  6,
        G =  7, H =  8, I =  9,
        K = 10, L = 11, M = 12,

        N = 13, O = 14, P = 15,
        Q = 16, R = 17, S = 18,
        T = 19, U = 20, V = 21,
        W = 22, X = 23, Y = 24,

        Z = 25,

        -- universal
        GMT = 186, UTC = 296, UT = 112,

        -- some non ambiguous
        MSK = 238,   MCK = 232,  CET = 155,
        AMDT = 336,  BDST = 344, IRKT = 409,
        KST = 226,   PDT = 264,  WET = 314,
        HOVDST = 664, CHODST = 656,

        -- Olson
        ['Europe/Moscow'] = 947,
        ['Africa/Abidjan'] = 672,
        ['America/Argentina/Buenos_Aires'] = 694,
        ['Asia/Krasnoyarsk'] = 861,
        ['Pacific/Fiji'] = 984,
    }
    local exp_pattern = '^2020%-02%-10T00:00'
    local base_date = '2020-02-10T0000 '

    for zone, index in pairs(zone_abbrevs) do
        local date_text = base_date .. zone
        local date, len = date.parse(date_text)
        print(zone, index)
        test:isnt(date, nil, 'parse ' .. zone)
        local tzname = date.tz
        local tzindex = date.tzindex
        test:is(tzindex, index, 'expected tzindex')
        test:is(tzname, zone, 'expected timezone name')
        test:is(TZ[tzindex], tzname, ('TZ[%d] => %s'):format(tzindex, tzname))
        test:is(TZ[tzname], tzindex, ('TZ[%s] => %d'):format(tzname, tzindex))
        test:ok(len > #base_date, 'length longer than ' .. #base_date)
        local txt = tostring(date)
        test:is(1, txt:find(exp_pattern), 'expected prefix')
        test:is(zone, txt:sub(#txt - #zone + 1, #txt), 'sub of ' .. txt)
        txt = date:format('%FT%T %Z')
        test:is(zone, txt:sub(#txt - #zone + 1, #txt), 'sub of ' .. txt)
    end
end)

local function error_ambiguous(s)
    return ("could not parse '%s' - ambiguous timezone"):format(s)
end

local function error_generic(s)
    return ("could not parse '%s'"):format(s)
end

test:test("Parsing of timezone names (errors)", function(test)
    test:plan(9)
    local zones_arratic = {
        -- ambiguous
        AT = error_ambiguous, BT = error_ambiguous,
        ACT = error_ambiguous, BST = error_ambiguous,
        GST = error_ambiguous, WAT = error_ambiguous,
        AZOST = error_ambiguous,
        -- generic errors
        ['XXX'] = error_generic,
        ['A-_'] = error_generic,
    }
    local base_date = '2020-02-10T0000 '

    for zone, error_function in pairs(zones_arratic) do
        local date_text = base_date .. zone
        assert_raises(test, error_function(date_text),
                      function() return date.parse(date_text) end)
    end
end)

test:test("Daylight saving checks", function (test)
    --[[
        Check various dates in `Europe/Moscow` timezone for their
        proper daylight saving settings.

        Tzdata defines these rules for `Europe/Moscow` time-zone:
```
Zone Europe/Moscow  2:30:17 -       LMT 1880
                    2:30:17 -       MMT 1916 Jul  3 # Moscow Mean Time
                    2:31:19 Russia  %s  1919 Jul  1  0:00u
                    3:00    Russia  %s  1921 10
                    3:00    Russia  Europe/Moscow/Europe/Moscow 1922 10
                    2:00    -       EET 1930 Jun 21
                    3:00    Russia  Europe/Moscow/Europe/Moscow 1991 03 31 2:00s
                    2:00    Russia  EE%sT 1992 Jan 19  2:00s
                    3:00    Russia  Europe/Moscow/Europe/Moscow 2011 03 27 2:00s
                    4:00    -       Europe/Moscow 2014 10 26 2:00s
                    3:00    -       Europe/Moscow
```
        Either you could see the same table dumped in more or less
        human-readable form using `zdump` utility:

        `zdump -c 2004,2022 -v Europe/Moscow`
    ]]
    test:plan(30)
    local moments = {
        -- string, isdst?, tzoffset (mins)
        {'2004-10-31T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2005-03-27T03:00:00 Europe/Moscow', true, 4 * 60},
        {'2005-10-30T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2006-03-26T03:00:00 Europe/Moscow', true, 4 * 60},
        {'2006-10-29T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2007-03-25T03:00:00 Europe/Moscow', true, 4 * 60},
        {'2007-10-28T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2008-03-30T03:00:00 Europe/Moscow', true, 4 * 60},
        {'2008-10-26T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2009-03-29T03:00:00 Europe/Moscow', true, 4 * 60},
        {'2009-10-25T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2010-03-28T03:00:00 Europe/Moscow', true, 4 * 60},
        {'2010-10-31T02:00:00 Europe/Moscow', false, 3 * 60},
        {'2011-03-27T03:00:00 Europe/Moscow', false, 4 * 60},
        {'2014-10-26T01:00:00 Europe/Moscow', false, 3 * 60},
    }
    for _, row in pairs(moments) do
        local str, isdst, tzoffset = unpack(row)
        local dt = date.parse(str)
        test:is(dt.isdst, isdst,
                ('%s: isdst = %s'):format(tostring(dt), dt.isdst))
        test:is(dt.tzoffset, tzoffset,
                ('%s: tzoffset = %s'):format(tostring(dt), dt.tzoffset))
    end
end)

test:test("Datetime string formatting", function(test)
    test:plan(11)
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
    test:is(t:format('%64424509441f'), '000000000', 'format #7')
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

local strftime_formats = {
    { '%A',                      1, 'Thursday' },
    { '%a',                      1, 'Thu' },
    { '%B',                      1, 'January' },
    { '%b',                      1, 'Jan' },
    { '%h',                      1, 'Jan' },
    { '%C',                      0, '19' },
    { '%c',                      1, 'Thu Jan  1 03:00:00 1970' },
    { '%D',                      1, '01/01/70' },
    { '%m/%d/%y',                1, '01/01/70' },
    { '%d',                      1, '01' },
    { '%Ec',                     1, 'Thu Jan  1 03:00:00 1970' },
    { '%EC',                     0, '19' },
    { '%Ex',                     1, '01/01/70' },
    { '%EX',                     1, '03:00:00' },
    { '%Ey',                     1, '70' },
    { '%EY',                     1, '1970' },
    { '%Od',                     1, '01' },
    { '%oe',                     0, 'oe' },
    { '%OH',                     1, '03' },
    { '%OI',                     1, '03' },
    { '%Om',                     1, '01' },
    { '%OM',                     1, '00' },
    { '%OS',                     1, '00' },
    { '%Ou',                     1, '4' },
    { '%OU',                     1, '00' },
    { '%OV',                     0, '01' },
    { '%Ow',                     1, '4' },
    { '%OW',                     1, '00' },
    { '%Oy',                     1, '70' },
    { '%e',                      1, ' 1' },
    { '%F',                      1, '1970-01-01' },
    { '%Y-%m-%d',                1, '1970-01-01' },
    { '%H',                      1, '03' },
    { '%I',                      1, '03' },
    { '%j',                      1, '001' },
    { '%k',                      1, ' 3' },
    { '%l',                      1, ' 3' },
    { '%M',                      1, '00' },
    { '%m',                      1, '01' },
    { '%n',                      1, '\n' },
    { '%p',                      1, 'AM' },
    { '%R',                      1, '03:00' },
    { '%H:%M',                   1, '03:00' },
    { '%r',                      1, '03:00:00 AM' },
    { '%I:%M:%S %p',             1, '03:00:00 AM' },
    { '%S',                      1, '00' },
    { '%s',                      1, '10800' },
    { '%f',                      1, '125' },
    { '%3f',                     0, '125' },
    { '%6f',                     0, '125000' },
    { '%6d',                     0, '6d' },
    { '%3D',                     0, '3D' },
    { '%T',                      1, '03:00:00' },
    { '%H:%M:%S',                1, '03:00:00' },
    { '%t',                      1, '\t' },
    { '%U',                      1, '00' },
    { '%u',                      1, '4' },
    { '%V',                      0, '01' },
    { '%G',                      1, '1970' },
    { '%g',                      1, '70' },
    { '%v',                      1, ' 1-Jan-1970' },
    { '%e-%b-%Y',                1, ' 1-Jan-1970' },
    { '%W',                      1, '00' },
    { '%w',                      1, '4' },
    { '%X',                      1, '03:00:00' },
    { '%x',                      1, '01/01/70' },
    { '%y',                      1, '70' },
    { '%Y',                      1, '1970' },
    { '%z',                      1, '+0300' },
    { '%%',                      1, '%' },
    { '%Y-%m-%dT%H:%M:%S.%9f%z', 1, '1970-01-01T03:00:00.125000000+0300' },
    { '%Y-%m-%dT%H:%M:%S.%f%z',  1, '1970-01-01T03:00:00.125+0300' },
    { '%Y-%m-%dT%H:%M:%S.%f',    1, '1970-01-01T03:00:00.125' },
    { '%FT%T.%f',                1, '1970-01-01T03:00:00.125' },
    { '%FT%T.%f%z',              1, '1970-01-01T03:00:00.125+0300' },
    { '%FT%T.%9f%z',             1, '1970-01-01T03:00:00.125000000+0300' },
}

test:test("Datetime string formatting detailed", function(test)
    test:plan(77)
    local T = date.new{ timestamp = 0.125 }
    T:set{ hour = 3, tzoffset = 180 }
    test:is(tostring(T), '1970-01-01T03:00:00.125+0300', 'tostring()')

    for _, row in pairs(strftime_formats) do
        local fmt, _, value = unpack(row)
        test:is(T:format(fmt), value,
                ('format %s, expected %s'):format(fmt, value))
    end
end)

test:test("Datetime string parsing by format (detailed)", function(test)
    test:plan(68)
    local T = date.new{ timestamp = 0.125 }
    T:set{ hour = 3, tzoffset = 180 }
    test:is(tostring(T), '1970-01-01T03:00:00.125+0300', 'tostring()')

    for _, row in pairs(strftime_formats) do
        local fmt, check, value = unpack(row)
        if check > 0 then
            local res = date.parse(value, {format = fmt})
            test:is(res ~= nil, true, ('parse of %s'):format(fmt))
        end
    end
end)

test:test("__index functions()", function(test)
    test:plan(16)
    -- 2000-01-29T03:30:12 MSK'
    local ts = date.new{sec = 12, min = 30, hour = 3,
                       tzoffset = 0,  day = 29, month = 1, year = 2000,
                       nsec = 123000000, tz = 'MSK'}

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
    test:is(ts.tzoffset, 180, "ts.tzoffset")
    test:is(ts.timestamp, 949105812.123, "ts.timestamp")

    test:is(ts.nsec, 123000000, 'ts.nsec')
    test:is(ts.usec, 123000, 'ts.usec')
    test:is(ts.msec, 123, 'ts.msec')
    test:is(ts.tz, 'MSK', 'ts.tz')
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
         '+10 weeks, 8 hours, 10 minutes, 30 seconds'},
        {{month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+20 months, 10 weeks, 8 hours, 10 minutes, 30 seconds'},
        {{year = 10, month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+10 years, 20 months, 10 weeks, 8 hours, 10 minutes, 30 seconds'},
        {{year = 1e4, month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+10000 years, 20 months, 10 weeks, 8 hours, 10 minutes, 30 seconds'},
        {{year = 5e6, month = 20, week = 10, hour = 8, min = 10, sec = 30},
         '+5000000 years, 20 months, 10 weeks, 8 hours, 10 minutes, 30 seconds'},
        {{year = -5e6, month = -20, week = -10, hour = -8, min = -10, sec = -30},
         '-5000000 years, -20 months, -10 weeks, -8 hours, -10 minutes, -30 seconds'},
        {{month = -20, week = -10, hour = -8, min = -10, sec = -30},
         '-20 months, -10 weeks, -8 hours, -10 minutes, -30 seconds'},
        {{week = -10, hour = -8, min = -10, sec = -30},
         '-10 weeks, -8 hours, -10 minutes, -30 seconds'},
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
    test:plan(14)

    local ival = date.interval.new{year = 12345, month = 123, week = 100,
                                   day = 45, hour = 48, min = 3, sec = 1,
                                   nsec = 12345678}
    test:is(tostring(ival), '+12345 years, 123 months, 100 weeks, 45 days, '..
            '48 hours, 3 minutes, 1 seconds, 12345678 nanoseconds',
            '__tostring')

    test:is(ival.nsec, 12345678, 'nsec')
    test:is(ival.usec, 12345, 'usec')
    test:is(ival.msec, 12, 'msec')

    test:is(ival.year, 12345, 'interval.year')
    test:is(ival.month, 123, 'interval.month')
    test:is(ival.week, 100, 'interval.week')
    test:is(ival.day, 45, 'interval.day')
    test:is(ival.hour, 48, 'interval.hour')
    test:is(ival.min, 3, 'interval.min')
    test:is(ival.sec, 1, 'interval.sec')
    local ival2 = date.interval.new{year = 12345, month = 123, week = 100,
                              day = 45, hour = 48, min = 3, sec = 1,
                              nsec = 12345678}
    test:is(ival, ival2, 'interval comparison eq')
    local ival3 = date.interval.new{year = -12345, month = 123, week = 100,
                              day = 45, hour = 48, min = 3, sec = 1}
    test:isnt(ival, ival3, 'interval comparison neq')
    test:is(ival3 < ival, true, 'interval less')
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
    test:is(tostring(i1 + i2), '+0 seconds, 1000000001 nanoseconds',
            'nsec: 999999999 + 2')
    i1 = ival_new{nsec = -1e9 + 1}
    i2 = ival_new{nsec = -2}
    test:is(tostring(i1 + i2), '+0 seconds, -1000000001 nanoseconds',
            'nsec: -999999999 - 2')

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

test:test("Time interval operations - different timezones", function(test)
    test:plan(8)
    local d1_msk = date.new{tz = 'MSK'}
    local d1_utc = date.new{tz = 'UTC'}
    test:is(d1_msk.tzoffset, 180, 'MSK is +180')
    test:is(d1_utc.tzoffset, 0, 'UTC is 0')
    test:is(tostring(d1_msk), '1970-01-01T00:00:00 MSK', '1970-01-01 MSK')
    test:is(tostring(d1_utc), '1970-01-01T00:00:00 UTC', '1970-01-01 UTC')
    test:is(d1_utc > d1_msk, true, 'utc > msk')
    test:is(tostring(d1_utc - d1_msk), '+180 minutes', 'utc - msk')
    test:is(tostring(d1_msk - d1_utc), '-180 minutes', 'msk - utc')
    test:is(d1_msk.epoch - d1_utc.epoch, -10800, '-10800')
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

test:test("Time intervals ops - huge values", function(test)
    test:plan(25)

    local huge_year = 3e6
    local huge_month = huge_year * 12
    local huge_days = huge_year * AVERAGE_DAYS_YEAR
    local huge_hours = huge_days * 24
    local huge_minutes = huge_hours * 60
    local huge_seconds = huge_minutes * 60

    local neg_year = date.new{year = -huge_year}
    test:is(tostring(neg_year), '-3000000-01-01T00:00:00Z', '-3000000')

    local checks = {
        {'year', 2 * huge_year, '+6000000 years', '3000000-01-01T00:00:00Z',
         MAX_YEAR_RANGE},
        {'month', 2 * huge_month, '+72000000 months', '3000000-01-01T00:00:00Z',
         MAX_MONTH_RANGE},
        {'day', 2 * huge_days, '+2191500000 days', '3000123-03-17T00:00:00Z',
         MAX_DAY_RANGE},
        {'hour', 2 * huge_hours, '+52596000000 hours',
         '3000123-03-17T00:00:00Z', MAX_HOUR_RANGE},
        {'min', 2 * huge_minutes, '+3155760000000 minutes',
         '3000123-03-17T00:00:00Z', MAX_MIN_RANGE},
        {'sec', 2 * huge_seconds, '+189345600000000 seconds',
         '3000123-03-17T00:00:00Z', MAX_SEC_RANGE},
    }
    local zero = date.interval.new()
    for _, row in pairs(checks) do
        local attr, huge, ival_txt, date_txt, max = unpack(row)
        local double_delta = date.interval.new{[attr] = huge}
        test:is(tostring(double_delta), ival_txt, ival_txt)
        local res = neg_year + double_delta
        test:is(tostring(res), date_txt, date_txt)
        assert_raises(test, ival_overflow('addition', attr, 2 * huge, max),
                      function() return double_delta + double_delta end)
        assert_raises(test, ival_overflow('subtraction', attr, -2 * huge, max),
                      function() return zero - double_delta - double_delta end)

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
    test:plan(79)

    -- check arithmetic with leap dates
    local T1970 = date.new{year = 1970, month = 1, day = 1}
    local T2000 = date.new{year = 2000, month = 1, day = 1,
                           tz = 'Europe/Moscow'}

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

    -- Check winter/DST sensitive operations with intervals.
    -- We use 2000 year here, because then Moscow still were
    -- switching between winter and summer time.
    local msk_offset = 180 -- expected winter time offset
    local msd_offset = 240 -- expected daylight saving time offset

    local res = T2000 + I1
    test:is(tostring(res), "2000-01-02T00:00:00 Europe/Moscow",
        "value: 2000 + I")
    test:is(res.tzoffset, msk_offset, "2000-01-02T00:00:00 - winter")

    res = T2000 + i1
    test:is(tostring(res), "2000-01-02T00:00:00 Europe/Moscow",
        "value: 2000 + i")
    test:is(res.tzoffset, msk_offset, "2000-01-02T00:00:00 - winter")

    res = T2000 + M2
    test:is(tostring(res), "2000-03-01T00:00:00 Europe/Moscow",
        "value: 2000 + 2M")
    test:is(res.tzoffset, msk_offset, "2000-03-01T00:00:00 - winter")

    res = T2000 + M2 + M2 + M2
    test:is(tostring(res), "2000-07-01T00:00:00 Europe/Moscow",
        "value: 2000 + 6M")
    test:is(res.tzoffset, msd_offset, "2000-07-01T00:00:00 - summer")

    res = T2000 + m2
    test:is(tostring(res), "2000-03-01T00:00:00 Europe/Moscow",
        "value: 2000 + 2m")
    test:is(res.tzoffset, msk_offset, "2000-03-01T00:00:00 - winter")

    res = T2000 + m2 + M2 + m2
    test:is(tostring(res), "2000-07-01T00:00:00 Europe/Moscow",
        "value: 2000 + 6m")
    test:is(res.tzoffset, msd_offset, "2000-07-01T00:00:00 - summer")

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
    test:plan(31)

    -- check arithmetic with leap dates
    local T1970 = date.new{year = 1970, month = 1, day = 1}
    local T2000 = date.new{year = 2000, month = 1, day = 1}
    local T1970S1 = date.new{year = 1970, month = 1, day = 1, sec = 1}
    local I1 = date.interval.new{day = 1}
    local M2 = date.interval.new{month = 2}
    local M10 = date.interval.new{month = 10}
    local Y1 = date.interval.new{year = 1}
    local Y5 = date.interval.new{year = 5}
    local NS1000 = date.interval.new{nsec = 1000}

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
    test:is(tostring(T1970 - NS1000), "1969-12-31T23:59:59.999999Z",
        "value: T - NS1000")
    test:is(tostring(T1970S1 - NS1000), "1970-01-01T00:00:00.999999Z",
        "value: T1970S1 - NS1000")
    test:is(tostring(T1970 - T2000), "-30 years", "value: T - T")
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

test:test("Parse iso8601 date - valid strings", function(test)
    test:plan(54)
    local good = {
        {2012, 12, 24, "20121224",                   8 },
        {2012, 12, 24, "20121224  Foo bar",          8 },
        {2012, 12, 24, "2012-12-24",                10 },
        {2012, 12, 24, "2012-12-24 23:59:59",       10 },
        {2012, 12, 24, "2012-12-24T00:00:00+00:00", 10 },
        {2012, 12, 24, "2012359",                    7 },
        {2012, 12, 24, "2012359T235959+0130",        7 },
        {2012, 12, 24, "2012-359",                   8 },
        {2012, 12, 24, "2012W521",                   8 },
        {2012, 12, 24, "2012-W52-1",                10 },
        {2012, 12, 24, "2012Q485",                   8 },
        {2012, 12, 24, "2012-Q4-85",                10 },
        {   1,  1,  1, "0001-Q1-01",                10 },
        {   1,  1,  1, "0001-W01-1",                10 },
        {   1,  1,  1, "0001-01-01",                10 },
        {   1,  1,  1, "0001-001",                   8 },
        {9999, 12, 31, "9999-12-31",                10 },
        {   0,  1,  1, "0000-Q1-01",                10 },
        {   0,  1,  3, "0000-W01-1",                10 },
        {   0,  1,  1, "0000-01-01",                10 },
        {   0,  1,  1, "0000-001",                   8 },
        {-200, 12, 31, "-200-12-31",                10 },
        {-1000,12, 31, "-1000-12-31",               11 },
        {-10000,12,31, "-10000-12-31",              12 },
        {-5879610,6,22,"-5879610-06-22",            14 },
        {10000, 1,  1, "10000-01-01",               11 },
        {5879611,7, 1, "5879611-07-01",             13 },
    }

    for _, value in ipairs(good) do
        local year, month, day, str, date_part_len
        year, month, day, str, date_part_len = unpack(value)
        local expected_date = date.new{year = year, month = month, day = day}
        local date_part, len
        date_part, len = date.parse_date(str)
        test:is(len, date_part_len, ('%s: length check %d'):format(str, len))
        test:is(expected_date, date_part, ('%s: expected date'):format(str))
    end
end)

test:test("Parse iso8601 date - invalid strings", function(test)
    test:plan(29)
    local bad = {
        "20121232"   , -- Invalid day of month
        "2012-12-310", -- Invalid day of month
        "2012-13-24" , -- Invalid month
        "2012367"    , -- Invalid day of year
        "2012-000"   , -- Invalid day of year
        "2012W533"   , -- Invalid week of year
        "2012-W52-8" , -- Invalid day of week
        "2012Q495"   , -- Invalid day of quarter
        "2012-Q5-85" , -- Invalid quarter
        "20123670"   , -- Trailing digit
        "201212320"  , -- Trailing digit
        "2012-12"    , -- Reduced accuracy
        "2012-Q4"    , -- Reduced accuracy
        "2012-Q42"   , -- Invalid
        "2012-Q1-1"  , -- Invalid day of quarter
        "2012Q--420" , -- Invalid
        "2012-Q-420" , -- Invalid
        "2012Q11"    , -- Incomplete
        "2012Q1234"  , -- Trailing digit
        "2012W12"    , -- Incomplete
        "2012W1234"  , -- Trailing digit
        "2012W-123"  , -- Invalid
        "2012-W12"   , -- Incomplete
        "2012-W12-12", -- Trailing digit
        "2012U1234"  , -- Invalid
        "2012-1234"  , -- Invalid
        "2012-X1234" , -- Invalid
        "-5879611-01-01",  -- Year less than 5879610-06-22
        "5879612-01-01",  -- Year greater than 5879611-07-11
    }

    for _, str in ipairs(bad) do
        assert_raises(test, invalid_date_fmt_error(str),
                      function() date.parse_date(str) end)
    end
end)

test:test("Parse tiny date into seconds and other parts", function(test)
    test:plan(4)
    local str = '19700101 00:00:30.528'
    local tiny = date.parse(str)
    test:is(tiny.epoch, 30, ("epoch of '%s'"):format(str))
    test:is(tiny.nsec, 528000000, ("nsec of '%s'"):format(str))
    test:is(tiny.sec, 30, "sec")
    test:is(tiny.timestamp, 30.528, "timestamp")
end)

test:test("Parse strptime format", function(test)
    test:plan(19)
    local formats = {
        {'Thu Jan  1 03:00:00 1970',    '%c',       '1970-01-01T03:00:00Z'},
        {'01/01/70',                    '%D',       '1970-01-01T00:00:00Z'},
        {'01/01/70',                    '%m/%d/%y', '1970-01-01T00:00:00Z'},
        {'Thu Jan  1 03:00:00 1970',    '%Ec',      '1970-01-01T03:00:00Z'},
        {'1970-01-01',                  '%F',       '1970-01-01T00:00:00Z'},
        {'1970-01-01',                  '%Y-%m-%d', '1970-01-01T00:00:00Z'},
        {' 1-Jan-1970',                 '%v',       '1970-01-01T00:00:00Z'},
        {' 1-Jan-1970',                 '%e-%b-%Y', '1970-01-01T00:00:00Z'},
        {'01/01/70',                    '%x',       '1970-01-01T00:00:00Z'},
        {'1970-01-01T0300+0300',        '%Y-%m-%dT%H%M%z',
            '1970-01-01T03:00:00+0300'},
        {'1970-01-01T03:00:00+0300',    '%Y-%m-%dT%H:%M:%S%z',
            '1970-01-01T03:00:00+0300'},
        {'1970-01-01T03:00:00.125000000+0300',  '%Y-%m-%dT%H:%M:%S.%f%z',
            '1970-01-01T03:00:00.125+0300'},
        {'1970-01-01T03:00:00.125+0300',        '%Y-%m-%dT%H:%M:%S.%f%z',
            '1970-01-01T03:00:00.125+0300'},
        {'1970-01-01T03:00:00.125',             '%Y-%m-%dT%H:%M:%S.%f',
            '1970-01-01T03:00:00.125Z'},
        {'1970-01-01T03:00:00.125',             '%FT%T.%f',
            '1970-01-01T03:00:00.125Z'},
        {'1970-01-01T03:00:00.125 MSK',         '%FT%T.%f %Z',
            '1970-01-01T03:00:00.125 MSK'},
        {'1970-01-01T03:00:00.125+0300',        '%FT%T.%f%z',
            '1970-01-01T03:00:00.125+0300'},
        {'1970-01-01T03:00:00.125000000MSK',    '%FT%T.%f%Z',
            '1970-01-01T03:00:00.125 MSK'},
        {'1970-01-01T03:00:00.125000000+0300',  '%FT%T.%f%z',
            '1970-01-01T03:00:00.125+0300'},
    }
    for _, row in pairs(formats) do
        local str, fmt, exp = unpack(row)
        local dt = date.parse(str, {format = fmt})
        test:is(tostring(dt), exp, ('parse %s via %s'):format(str, fmt))
    end
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
    test:plan(15)

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
    test:is(tostring(ts:set{ min = 12, sec = 23 }), '2020-11-09T06:12:23+0300',
            'min 12, sec 23')
    test:is(tostring(ts:set{ tzoffset = -8*60 }), '2020-11-09T06:12:23-0800',
            'offset -0800' )
    test:is(tostring(ts:set{ tzoffset = '+0800' }), '2020-11-09T06:12:23+0800',
            'offset +0800' )
    -- timestamp 1630359071.125 is 2021-08-30T21:31:11.125Z
    test:is(tostring(ts:set{ timestamp = 1630359071.125 }),
            '2021-08-30T21:31:11.125+0800', 'timestamp 1630359071.125' )
    test:is(tostring(ts:set{ msec = 123}), '2021-08-30T21:31:11.123+0800',
            'msec = 123')
    test:is(tostring(ts:set{ usec = 123}), '2021-08-30T21:31:11.000123+0800',
            'usec = 123')
    test:is(tostring(ts:set{ nsec = 123}), '2021-08-30T21:31:11.000000123+0800',
            'nsec = 123')
    test:is(tostring(ts:set{timestamp = 1630359071, msec = 123}),
            '2021-08-30T21:31:11.123+0800', 'timestamp + msec')
    test:is(tostring(ts:set{timestamp = 1630359071, usec = 123}),
            '2021-08-30T21:31:11.000123+0800', 'timestamp + usec')
    test:is(tostring(ts:set{timestamp = 1630359071, nsec = 123}),
            '2021-08-30T21:31:11.000000123+0800', 'timestamp + nsec')
end)

test:test("Check :set{} and .new{} equal for all attributes", function(test)
    test:plan(11)
    local ts, ts2
    local obj = {}
    local attribs = {
        {'year', 2000},
        {'month', 11},
        {'day', 30},
        {'hour', 6},
        {'min', 12},
        {'sec', 23},
        {'tzoffset', -8*60},
        {'tzoffset', '+0800'},
        {'tz', 'MSK'},
        {'nsec', 560000},
    }
    for _, row in pairs(attribs) do
        local key, value = unpack(row)
        obj[key] = value
        ts = date.new(obj)
        ts2 = date.new():set(obj)
        test:is(ts, ts2, ('[%s] = %s (%s = %s)'):
                format(key, tostring(value), tostring(ts), tostring(ts2)))
    end

    obj = {timestamp = 1630359071.125, tzoffset = '+0800'}
    ts = date.new(obj)
    ts2 = date.new():set(obj)
    test:is(ts, ts2, ('timestamp+tzoffset (%s = %s)'):
            format(tostring(ts), tostring(ts2)))
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
        {expected_str('parse_tzname()', 400), {tz = 400}},
        {table_expected('datetime.set()', '2001-01-01'), '2001-01-01'},
        {table_expected('datetime.set()', 20010101), 20010101},
        {range_check_3_error('day', 32, {-1, 1, 31}),
            {year = 2021, month = 6, day = 32}},
        {invalid_days_in_mon(31, 6, 2021), { month = 6, day = 31}},
        {invalid_date(-5879610, 6, 21),
            {year = -5879610, month = 6, day = 21}},
        {invalid_date(-5879610, 1, 1),
            {year = -5879610, month = 1, day = 1}},
        {range_check_error('year', -16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = -16009610, month = 12, day = 31}},
        {range_check_error('year', 16009610, {MIN_DATE_YEAR, MAX_DATE_YEAR}),
            {year = 16009610, month = 1, day = 1}},
        {invalid_date(MAX_DATE_YEAR, 9, 1),
            {year = MAX_DATE_YEAR, month = 9, day = 1}},
        {invalid_date(MAX_DATE_YEAR, 7, 12),
            {year = MAX_DATE_YEAR, month = 7, day = 12}},
    }
    for _, row in pairs(specific_errors) do
        local err_msg, attribs = unpack(row)
        assert_raises(test, err_msg, function() ts:set(attribs) end)
    end
end)

test:test("Time invalid tzoffset in :set{} operations", function(test)
    test:plan(13)

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
