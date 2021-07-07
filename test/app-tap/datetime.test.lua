#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("errno")
local date = require('datetime')

test:plan(5)

test:test("Simple tests for parser", function(test)
    test:plan(2)
    test:ok(date("1970-01-01T01:00:00Z") ==
            date {year=1970, month=1, day=1, hour=1, minute=0, second=0})
    test:ok(date("1970-01-01T02:00:00+02:00") ==
            date {year=1970, month=1, day=1, hour=2, minute=0, second=0, tz=120})
end)

test:test("Multiple tests for parser (with nanoseconds)", function(test)
    test:plan(165)
    -- borrowed from p5-time-moments/t/180_from_string.t
    local tests =
    {
        { '1970-01-01T00:00:00Z',                       0,           0,    0 },
        { '1970-01-01T02:00:00+02:00',                  0,           0,  120 },
        { '1970-01-01T01:30:00+01:30',                  0,           0,   90 },
        { '1970-01-01T01:00:00+01:00',                  0,           0,   60 },
        { '1970-01-01T00:01:00+00:01',                  0,           0,    1 },
        { '1970-01-01T00:00:00+00:00',                  0,           0,    0 },
        { '1969-12-31T23:59:00-00:01',                  0,           0,   -1 },
        { '1969-12-31T23:00:00-01:00',                  0,           0,  -60 },
        { '1969-12-31T22:30:00-01:30',                  0,           0,  -90 },
        { '1969-12-31T22:00:00-02:00',                  0,           0, -120 },
        { '1970-01-01T00:00:00.123456789Z',             0,   123456789,    0 },
        { '1970-01-01T00:00:00.12345678Z',              0,   123456780,    0 },
        { '1970-01-01T00:00:00.1234567Z',               0,   123456700,    0 },
        { '1970-01-01T00:00:00.123456Z',                0,   123456000,    0 },
        { '1970-01-01T00:00:00.12345Z',                 0,   123450000,    0 },
        { '1970-01-01T00:00:00.1234Z',                  0,   123400000,    0 },
        { '1970-01-01T00:00:00.123Z',                   0,   123000000,    0 },
        { '1970-01-01T00:00:00.12Z',                    0,   120000000,    0 },
        { '1970-01-01T00:00:00.1Z',                     0,   100000000,    0 },
        { '1970-01-01T00:00:00.01Z',                    0,    10000000,    0 },
        { '1970-01-01T00:00:00.001Z',                   0,     1000000,    0 },
        { '1970-01-01T00:00:00.0001Z',                  0,      100000,    0 },
        { '1970-01-01T00:00:00.00001Z',                 0,       10000,    0 },
        { '1970-01-01T00:00:00.000001Z',                0,        1000,    0 },
        { '1970-01-01T00:00:00.0000001Z',               0,         100,    0 },
        { '1970-01-01T00:00:00.00000001Z',              0,          10,    0 },
        { '1970-01-01T00:00:00.000000001Z',             0,           1,    0 },
        { '1970-01-01T00:00:00.000000009Z',             0,           9,    0 },
        { '1970-01-01T00:00:00.00000009Z',              0,          90,    0 },
        { '1970-01-01T00:00:00.0000009Z',               0,         900,    0 },
        { '1970-01-01T00:00:00.000009Z',                0,        9000,    0 },
        { '1970-01-01T00:00:00.00009Z',                 0,       90000,    0 },
        { '1970-01-01T00:00:00.0009Z',                  0,      900000,    0 },
        { '1970-01-01T00:00:00.009Z',                   0,     9000000,    0 },
        { '1970-01-01T00:00:00.09Z',                    0,    90000000,    0 },
        { '1970-01-01T00:00:00.9Z',                     0,   900000000,    0 },
        { '1970-01-01T00:00:00.99Z',                    0,   990000000,    0 },
        { '1970-01-01T00:00:00.999Z',                   0,   999000000,    0 },
        { '1970-01-01T00:00:00.9999Z',                  0,   999900000,    0 },
        { '1970-01-01T00:00:00.99999Z',                 0,   999990000,    0 },
        { '1970-01-01T00:00:00.999999Z',                0,   999999000,    0 },
        { '1970-01-01T00:00:00.9999999Z',               0,   999999900,    0 },
        { '1970-01-01T00:00:00.99999999Z',              0,   999999990,    0 },
        { '1970-01-01T00:00:00.999999999Z',             0,   999999999,    0 },
        { '1970-01-01T00:00:00.0Z',                     0,           0,    0 },
        { '1970-01-01T00:00:00.00Z',                    0,           0,    0 },
        { '1970-01-01T00:00:00.000Z',                   0,           0,    0 },
        { '1970-01-01T00:00:00.0000Z',                  0,           0,    0 },
        { '1970-01-01T00:00:00.00000Z',                 0,           0,    0 },
        { '1970-01-01T00:00:00.000000Z',                0,           0,    0 },
        { '1970-01-01T00:00:00.0000000Z',               0,           0,    0 },
        { '1970-01-01T00:00:00.00000000Z',              0,           0,    0 },
        { '1970-01-01T00:00:00.000000000Z',             0,           0,    0 },
        { '1973-11-29T21:33:09Z',               123456789,           0,    0 },
        { '2013-10-28T17:51:56Z',              1382982716,           0,    0 },
        -- { '9999-12-31T23:59:59Z',            253402300799,           0,    0 },
    }
    for _, value in ipairs(tests) do
        local str, epoch, nsec, offset
        str, epoch, nsec, offset = unpack(value)
        local dt = date(str)
        test:ok(dt.secs == epoch, ('%s: dt.secs == %d'):format(str, epoch))
        test:ok(dt.nsec == nsec, ('%s: dt.nsec == %d'):format(str, nsec))
        test:ok(dt.offset == offset, ('%s: dt.offset == %d'):format(str, offset))
    end
end)

local ffi = require('ffi')

ffi.cdef [[
    void tzset(void);
]]

test:test("Datetime string formatting", function(test)
    -- redefine timezone to be always GMT-2
    test:plan(7)
    local str = "1970-01-01"
    local t = date(str)
    test:ok(t.secs == 0, ('%s: t.secs == %d'):format(str, t.secs))
    test:ok(t.nsec == 0, ('%s: t.nsec == %d'):format(str, t.nsec))
    test:ok(t.offset == 0, ('%s: t.offset == %d'):format(str, t.offset))
    test:ok(date.asctime(t) == 'Thu Jan  1 00:00:00 1970\n', ('%s: asctime'):format(str))
    os.setenv('TZ', 'GMT-2')
    ffi.C.tzset()
    test:ok(date.ctime(t) == 'Thu Jan  1 02:00:00 1970\n', ('%s: ctime with timezone'):format(str))
    test:ok(date.strftime('%d/%m/%Y', t) == '01/01/1970', ('%s: strftime #1'):format(str))
    test:ok(date.strftime('%A %d. %B %Y', t) == 'Thursday 01. January 1970', ('%s: strftime #2'):format(str))
end)

test:test("Parse iso date - valid strings", function(test)
    test:plan(32)
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
    }

    for _, value in ipairs(good) do
        local year, month, day, str, date_part_len;
        year, month, day, str, date_part_len = unpack(value)
        local expected_date = date{year = year, month = month, day = day}
        local date_part, len
        date_part, len = date.parse_date(str)
        test:ok(len == date_part_len, ('%s: length check %d'):format(str, len))
        test:ok(expected_date == date_part, ('%s: expected date'):format(str))
    end
end)

test:test("Parse iso date - invalid strings", function(test)
    test:plan(62)
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
        "0000-Q1-01" , -- Year less than 0001
        "0000-W01-1" , -- Year less than 0001
        "0000-01-01" , -- Year less than 0001
        "0000-001"   , -- Year less than 0001
    }

    for _, str in ipairs(bad) do
        local date_part, len
        date_part, len = date.parse_date(str)
        test:ok(len == 0, ('%s: length check %d'):format(str, len))
        test:ok(date_part == nil, ('%s: empty date check %s'):format(str, date_part))
    end
end)

os.exit(test:check() and 0 or 1)
