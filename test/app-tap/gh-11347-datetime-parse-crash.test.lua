#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('errno')
local date = require('datetime')

test:plan(1)

local function assert_raises(test, error_msg, func, ...)
    local ok, err = pcall(func, ...)
    local err_tail = err and err:gsub("^.+:%d+: ", "") or ''
    return test:is(not ok and err_tail, error_msg,
                   ('"%s" received, "%s" expected'):format(err_tail, error_msg))
end

local function error_could_not_parse_format(s, fmt)
    return ("could not parse '%s' using '%s' format"):format(s, fmt)
end

--[[
Test for bug fix #11347.

Checks ambiguous case where day of year (yday, which is defines
calendar month and month day implicitly) and calendar month
without a day are both defined in the date text.

Expects an error instead of assertion crash.
]]
test:test("Parse invalid string with a custom format", function(test)
    local formats = {
        -- Issue 11347 reported case:
        -- (month = 07, year = 00, day of year = 1)
        -- Month of day of year 1 is 01,
        -- but 07 is given.
        {
            fmt = "%m%g%j",
            buf = "07001",
        },
        -- 2025-321 is 2025-11-17.
        {
            fmt = "%G-%j %m",
            buf = "2025-321 01",
        },
        {
            fmt = "%G-%j %m",
            buf = "2025-321 10",
        },
        {
            fmt = "%G-%j %m",
            buf =  "2025-321 12",
        },
    }
    test:plan(#formats)
    for _, tc in pairs(formats) do
        assert_raises(test,
                      error_could_not_parse_format(tc.buf, tc.fmt),
                      function() date.parse(tc.buf, {format = tc.fmt}) end)
    end
end)

os.exit(test:check() and 0 or 1)
