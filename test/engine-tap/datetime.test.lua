#!/usr/bin/env tarantool

package.path = "lib/?.lua;"..package.path

local test = require('tester')
local date = require('datetime')

-- minimum supported date - -5879610-06-22
local MIN_DATE_YEAR = -5879610
local MIN_DATE_MONTH = 6
local MIN_DATE_DAY = 22
-- maximum supported date - 5879611-07-11
local MAX_DATE_YEAR = 5879611
local MAX_DATE_MONTH = 7
local MAX_DATE_DAY = 11

test:plan(2)

local engine = test:engine()

local function create_datetime_space(name)
    local space = box.schema.space.create(name, {engine = engine})
    space:create_index('pk', {parts={1,'datetime'}})
    return space
end

local function check_order(test, tuples)
    for i = 1, #tuples - 1 do
        test:ok(tuples[i][1] < tuples[i + 1][1],
                ('%s < %s'):format(tuples[i][1], tuples[i + 1][1]))
    end
end

test:test("Simple tests for datetime indices", function(test)
    test:plan(23)
    local tmp = create_datetime_space('T1')

    tmp:insert{date.new{year = 1970, month = 1, day = 1}}
    tmp:insert{date.new{year = 1970, month = 1, day = 2}}
    tmp:insert{date.new{year = 1970, month = 1, day = 3}}
    tmp:insert{date.new{year = 2000, month = 1, day = 1}}

    local rs = tmp:select{}
    test:is(tostring(rs[1][1]), '1970-01-01T00:00:00Z')
    test:is(tostring(rs[2][1]), '1970-01-02T00:00:00Z')
    test:is(tostring(rs[3][1]), '1970-01-03T00:00:00Z')
    test:is(tostring(rs[4][1]), '2000-01-01T00:00:00Z')

    for _ = 1,16 do
        tmp:insert{date.now()}
    end
    check_order(test, tmp:select{})

    tmp:drop()
end)

test:test("Check order for hints overflow/underflow", function(test)
    test:plan(49)
    local tmp = create_datetime_space('T2')

    --[[
        range of dates, supported by hints without any loss
        is -208-05-13T16:27:44Z .. 6325-04-08T15:04:31Z
    ]]
    local years = {
        {
            year = MIN_DATE_YEAR, month = MIN_DATE_MONTH, day = MIN_DATE_DAY,
            hour = 23, min = 59, sec = 59,
        },
        {year = -5000000},
        {year = -1000000},
        {year = -100000},
        {year = -1000},
        {year = -208, month = 5, day = 13},
        {year = -208, month = 5, day = 14},
        {year = -100},
        {year = -1},
        {year = -1, month = 12, day = 31},
        {year = 0},
        {year = 0, month = 12, day = 31},
        {year = 1},
        {year = 100},
        {year = 1000},
        {year = 1969, month = 12, day = 31},
        {year = 1970, month = 1, day = 1},
        {year = 4000},
        {year = 6000},
        {year = 6325, month = 4, day = 8},
        {year = 6325, month = 4, day = 9},
        {year = 10000},
        {year = 1000000},
        {year = 5000000},
        {year = MAX_DATE_YEAR, month = MAX_DATE_MONTH, day = MAX_DATE_DAY},
    }

    for _, row in pairs(years) do
        local dt = date.new(row)
        test:is(dt ~= nil, true, ('object created %s'):format(dt))
        tmp:insert{dt}
    end

    check_order(test, tmp:select{})

    tmp:drop()
end)

os.exit(test:check() and 0 or 1)
