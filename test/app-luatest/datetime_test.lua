local t = require('luatest')
local dt = require('datetime')
local fun = require('fun')
local checks = require('checks')
local compat = require('compat')

-- {{{ Datetime module and related helper constants.

local INT_MAX = 2147483647
local INT_MIN = -2147483648
local SECS_PER_DAY = 86400

local DAYS_EPOCH_OFFSET = 719163
local SECS_EPOCH_OFFSET = DAYS_EPOCH_OFFSET * SECS_PER_DAY
local MIN_DT_DAY_VALUE = INT_MIN
local MAX_DT_DAY_VALUE = INT_MAX
local MIN_EPOCH_SECS_VALUE = MIN_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_OFFSET
local MAX_EPOCH_SECS_VALUE = MAX_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_OFFSET

-- Minimum supported date: -5879610-06-22.
local MIN_DATE_YEAR = -5879610
local MIN_DATE_MONTH = 6
local MIN_DATE_DAY = 22
-- Maximum supported date: 5879611-07-11.
local MAX_DATE_YEAR = 5879611
local MAX_DATE_MONTH = 7
local MAX_DATE_DAY = 11

local MIN_TZOFFSET = -12 * 60
local MAX_TZOFFSET = 14 * 60
local MIN_TZOFFSET_H = MIN_TZOFFSET / 60
local MAX_TZOFFSET_H = MAX_TZOFFSET / 60

local YEAR_RANGE = {MIN_DATE_YEAR, MAX_DATE_YEAR}
local MONTH_RANGE = {1, 12}
local DAY_RANGE = {1, 31}
local HOUR_RANGE = {0, 23}
local MINUTE_RANGE = {0, 59}
local SEC_RANGE = {0, 60}
local TIMESTAMP_RANGE = {MIN_EPOCH_SECS_VALUE, MAX_EPOCH_SECS_VALUE}
local MSEC_RANGE = {0, 1E3}
local USEC_RANGE = {0, 1E6}
local NSEC_RANGE = {0, 1E9}
local TZOFFSET_RANGE = {MIN_TZOFFSET, MAX_TZOFFSET}

-- }}} Datetime module and related helper constants.

-- {{{ Common utils.

local function get_single_key_val(arg, table_expected)
    local key, val
    if type(arg) == 'table' then
        local count = 0
        for k, v in pairs(arg) do
            key, val = k, v
            count = count + 1
        end
        t.fail_if(key == nil, 'misconfig: expected table {key = val}')
        t.fail_if(val == nil, 'misconfig: expected table {key = val}')
        t.fail_if(count > 1, 'misconfig: expected table {key = val}')
    else
        t.fail_if(table_expected, 'misconfig: expected table')
        key = nil
        val = arg
    end
    return key, val
end

-- See ISO 8601-1:2019 5.3.4.1 for time shift format.
local function tzoffset_fmt(x, shift_type)
    checks('int64', 'string')
    local h = x / 60
    local m = math.abs(x) % 60
    if shift_type == '' then
        -- 'shift' format.
        return ('%+03d%02d'):format(h, m)
    elseif shift_type == 'X' then
        -- 'shiftX' format.
        return ('%+03d:%02d'):format(h, m)
    elseif shift_type == 'H' then
        -- 'shiftH' format.
        assert(m == 0)
        return ('%+03d'):format(h)
    end
    error('invalid shift_type')
end

-- }}} Common utils.

local SUPPORTED_DATETIME_FORMATS = {
    ['RFC3339 AND ISO8601'] = {
        -- Dates.
        {
            fmt = '%Y-%M-%D',
            buf = '2024-07-31',
        },
        -- Date-Times.
        {
            fmt = '%Y-%M-%DT%h:%m:%sZ',
            buf = '2024-07-31T14:30:02Z',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.1sZ',
            buf = '2024-07-31T14:30:02.1Z',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.2sZ',
            buf = '2024-07-31T14:30:02.13Z',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3sZ',
            buf = '2024-07-31T14:30:02.132Z',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s.%uZ',
            buf = '2024-07-31T14:30:02.132209Z',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s%Z:%z',
            buf = '2024-07-31T17:30:02+03:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3s%Z:%z',
            buf = '2024-07-31T17:30:02.132+03:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s.%u%Z:%z',
            buf = '2024-07-31T17:30:02.132209+03:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s+08:45',
            buf = '2024-07-31T23:15:02+08:45',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s+00:00',
            buf = '2024-07-31T14:30:02+00:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3s+00:00',
            buf = '2024-07-31T14:30:02.132+00:00',
        },
    },

    ['RFC3339 ONLY'] = {
        -- Dates-Times.
        {
            fmt = '%Y-%M-%Dt%h:%m:%sz',
            buf = '2024-07-31t14:30:02z',
        }, {
            fmt = '%Y-%M-%Dt%h:%m:%.3sz',
            buf = '2024-07-31t14:30:02.132z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%s%Z:%z',
            buf = '2024-07-31 17:30:02+03:00',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.1s%Z:%z',
            buf = '2024-07-31 17:30:02.1+03:00',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.2s%Z:%z',
            buf = '2024-07-31 17:30:02.13+03:00',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.3s%Z:%z',
            buf = '2024-07-31 17:30:02.132+03:00',
        }, {
            fmt = '%Y-%M-%D %h:%m:%s.%u%Z:%z',
            buf = '2024-07-31 17:30:02.132209+03:00',
        }, {
            fmt = '%Y-%M-%D %h:%m:%sZ',
            buf = '2024-07-31 14:30:02Z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%sz',
            buf = '2024-07-31 14:30:02z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.1sZ',
            buf = '2024-07-31 14:30:02.1Z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.2sZ',
            buf = '2024-07-31 14:30:02.13Z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.3sZ',
            buf = '2024-07-31 14:30:02.132Z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%s.%uZ',
            buf = '2024-07-31 14:30:02.132209Z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.3sz',
            buf = '2024-07-31 14:30:02.132z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%s.%uz',
            buf = '2024-07-31 14:30:02.132209z',
        }, {
            fmt = '%Y-%M-%D %h:%m:%s-00:00',
            buf = '2024-07-31 14:30:02-00:00',
        }, {
            fmt = '%Y-%M-%D %h:%m:%.3s-00:00',
            buf = '2024-07-31 14:30:02.132-00:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s-00:00',
            buf = '2024-07-31T14:30:02-00:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3s-00:00',
            buf = '2024-07-31T14:30:02.132-00:00',
        },
    },

    ['ISO8601 ONLY'] = {
        -- Dates.
        {
            fmt = '%Y-%O',
            buf = '2024-213',
        }, {
            fmt = '%V-W%W-%w',
            buf = '2024-W31-3',
        }, {
            fmt = '%Y%M%D',
            buf = '20240731',
        }, {
            fmt = '%Y%O',
            buf = '2024213',
        }, {
            fmt = '%VW%W%w',
            buf = '2024W313',
        },
        -- Dates-Times.
        {
            fmt = '%Y-%M-%DT%h',
            buf = '2024-07-31T17',
        }, {
            fmt = '%Y-%M-%DT%,1h',
            buf = '2024-07-31T17,5',
            ts = 1722447000,
        }, {
            fmt = '%Y-%M-%DT%.1h',
            buf = '2024-07-31T17.5',
            ts = 1722447000,
        }, {
            fmt = '%Y-%M-%DT%h:%m',
            buf = '2024-07-31T17:30',
            ts = 1722447000,
        }, {
            fmt = '%Y-%M-%DT%h:%,1m',
            buf = '2024-07-31T17:30,0',
            ts = 1722447000,
        }, {
            fmt = '%Y-%M-%DT%h:%.1m',
            buf = '2024-07-31T17:30.0',
            ts = 1722447000,
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s',
            buf = '2024-07-31T17:30:02',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.1s',
            buf = '2024-07-31T17:30:02.1',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.2s',
            buf = '2024-07-31T17:30:02.13',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%,3s',
            buf = '2024-07-31T17:30:02,132',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3s',
            buf = '2024-07-31T17:30:02.132',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s,%u',
            buf = '2024-07-31T17:30:02,132209',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s.%u',
            buf = '2024-07-31T17:30:02.132209',
        }, {
            fmt = '%Y-%M-%DT%hZ',
            buf = '2024-07-31T14Z',
        }, {
            fmt = '%Y-%M-%DT%,1hZ',
            buf = '2024-07-31T14,5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%.1hZ',
            buf = '2024-07-31T14.5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%mZ',
            buf = '2024-07-31T14:30Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%,1mZ',
            buf = '2024-07-31T14:30,0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%.1mZ',
            buf = '2024-07-31T14:30.0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%m:%,3sZ',
            buf = '2024-07-31T14:30:02,132Z',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s,%uZ',
            buf = '2024-07-31T14:30:02,132209Z',
        }, {
            fmt = '%Y-%M-%DT%h%Z',
            buf = '2024-07-31T17+03',
        }, {
            fmt = '%Y-%M-%DT%,1h%Z',
            buf = '2024-07-31T17,5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%.1h%Z',
            buf = '2024-07-31T17.5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%m%Z',
            buf = '2024-07-31T17:30+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%,1m%Z',
            buf = '2024-07-31T17:30,0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%.1m%Z',
            buf = '2024-07-31T17:30.0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s%Z',
            buf = '2024-07-31T17:30:02+03',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.1s%Z',
            buf = '2024-07-31T17:30:02.1+03',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.2s%Z',
            buf = '2024-07-31T17:30:02.13+03',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%,3s%Z',
            buf = '2024-07-31T17:30:02,132+03',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3s%Z',
            buf = '2024-07-31T17:30:02.132+03',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s,%u%Z',
            buf = '2024-07-31T17:30:02,132209+03',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s.%u%Z',
            buf = '2024-07-31T17:30:02.132209+03',
        }, {
            fmt = '%Y-%M-%DT%h%Z:%z',
            buf = '2024-07-31T17+03:00',
        }, {
            fmt = '%Y-%M-%DT%,1h%Z:%z',
            buf = '2024-07-31T17,5+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%.1h%Z:%z',
            buf = '2024-07-31T17.5+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%m%Z:%z',
            buf = '2024-07-31T17:30+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%,1m%Z:%z',
            buf = '2024-07-31T17:30,0+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%.1m%Z:%z',
            buf = '2024-07-31T17:30.0+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.1s%Z:%z',
            buf = '2024-07-31T17:30:02.1+03:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.2s%Z:%z',
            buf = '2024-07-31T17:30:02.13+03:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%,3s%Z:%z',
            buf = '2024-07-31T17:30:02,132+03:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s,%u%Z:%z',
            buf = '2024-07-31T17:30:02,132209+03:00',
        }, {
            fmt = '%V-W%W-%wT%h',
            buf = '2024-W31-3T17',
        }, {
            fmt = '%V-W%W-%wT%,1h',
            buf = '2024-W31-3T17,5',
            ts = 1722447000,
        }, {
            fmt = '%V-W%W-%wT%.1h',
            buf = '2024-W31-3T17.5',
            ts = 1722447000,
        }, {
            fmt = '%V-W%W-%wT%h:%m',
            buf = '2024-W31-3T17:30',
            ts = 1722447000,
        }, {
            fmt = '%V-W%W-%wT%h:%,1m',
            buf = '2024-W31-3T17:30,0',
            ts = 1722447000,
        }, {
            fmt = '%V-W%W-%wT%h:%.1m',
            buf = '2024-W31-3T17:30.0',
            ts = 1722447000,
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s',
            buf = '2024-W31-3T17:30:02',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.1s',
            buf = '2024-W31-3T17:30:02.1',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.2s',
            buf = '2024-W31-3T17:30:02.13',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%,3s',
            buf = '2024-W31-3T17:30:02,132',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.3s',
            buf = '2024-W31-3T17:30:02.132',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s,%u',
            buf = '2024-W31-3T17:30:02,132209',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s.%u',
            buf = '2024-W31-3T17:30:02.132209',
        }, {
            fmt = '%V-W%W-%wT%hZ',
            buf = '2024-W31-3T14Z',
        }, {
            fmt = '%V-W%W-%wT%,1hZ',
            buf = '2024-W31-3T14,5Z',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%.1hZ',
            buf = '2024-W31-3T14.5Z',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%mZ',
            buf = '2024-W31-3T14:30Z',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%,1mZ',
            buf = '2024-W31-3T14:30,0Z',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%.1mZ',
            buf = '2024-W31-3T14:30.0Z',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%m:%sZ',
            buf = '2024-W31-3T14:30:02Z',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.1sZ',
            buf = '2024-W31-3T14:30:02.1Z',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.2sZ',
            buf = '2024-W31-3T14:30:02.13Z',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%,3sZ',
            buf = '2024-W31-3T14:30:02,132Z',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.3sZ',
            buf = '2024-W31-3T14:30:02.132Z',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s,%uZ',
            buf = '2024-W31-3T14:30:02,132209Z',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s.%uZ',
            buf = '2024-W31-3T14:30:02.132209Z',
        }, {
            fmt = '%V-W%W-%wT%h%Z',
            buf = '2024-W31-3T17+03',
        }, {
            fmt = '%V-W%W-%wT%,1h%Z',
            buf = '2024-W31-3T17,5+03',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%.1h%Z',
            buf = '2024-W31-3T17.5+03',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%m%Z',
            buf = '2024-W31-3T17:30+03',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%,1m%Z',
            buf = '2024-W31-3T17:30,0+03',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%.1m%Z',
            buf = '2024-W31-3T17:30.0+03',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s%Z',
            buf = '2024-W31-3T17:30:02+03',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.1s%Z',
            buf = '2024-W31-3T17:30:02.1+03',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.2s%Z',
            buf = '2024-W31-3T17:30:02.13+03',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%,3s%Z',
            buf = '2024-W31-3T17:30:02,132+03',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.3s%Z',
            buf = '2024-W31-3T17:30:02.132+03',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s,%u%Z',
            buf = '2024-W31-3T17:30:02,132209+03',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s.%u%Z',
            buf = '2024-W31-3T17:30:02.132209+03',
        }, {
            fmt = '%V-W%W-%wT%h%Z:%z',
            buf = '2024-W31-3T17+03:00',
        }, {
            fmt = '%V-W%W-%wT%,1h%Z:%z',
            buf = '2024-W31-3T17,5+03:00',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%.1h%Z:%z',
            buf = '2024-W31-3T17.5+03:00',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%m%Z:%z',
            buf = '2024-W31-3T17:30+03:00',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%,1m%Z:%z',
            buf = '2024-W31-3T17:30,0+03:00',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%.1m%Z:%z',
            buf = '2024-W31-3T17:30.0+03:00',
            ts = 1722436200,
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s%Z:%z',
            buf = '2024-W31-3T17:30:02+03:00',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.1s%Z:%z',
            buf = '2024-W31-3T17:30:02.1+03:00',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.2s%Z:%z',
            buf = '2024-W31-3T17:30:02.13+03:00',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%,3s%Z:%z',
            buf = '2024-W31-3T17:30:02,132+03:00',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.3s%Z:%z',
            buf = '2024-W31-3T17:30:02.132+03:00',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s,%u%Z:%z',
            buf = '2024-W31-3T17:30:02,132209+03:00',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s.%u%Z:%z',
            buf = '2024-W31-3T17:30:02.132209+03:00',
        }, {
            fmt = '%Y-%OT%h',
            buf = '2024-213T17',
        }, {
            fmt = '%Y-%OT%,1h',
            buf = '2024-213T17,5',
            ts = 1722447000,
        }, {
            fmt = '%Y-%OT%.1h',
            buf = '2024-213T17.5',
            ts = 1722447000,
        }, {
            fmt = '%Y-%OT%h:%m',
            buf = '2024-213T17:30',
            ts = 1722447000,
        }, {
            fmt = '%Y-%OT%h:%,1m',
            buf = '2024-213T17:30,0',
            ts = 1722447000,
        }, {
            fmt = '%Y-%OT%h:%.1m',
            buf = '2024-213T17:30.0',
            ts = 1722447000,
        }, {
            fmt = '%Y-%OT%h:%m:%s',
            buf = '2024-213T17:30:02',
        }, {
            fmt = '%Y-%OT%h:%m:%.1s',
            buf = '2024-213T17:30:02.1',
        }, {
            fmt = '%Y-%OT%h:%m:%.2s',
            buf = '2024-213T17:30:02.13',
        }, {
            fmt = '%Y-%OT%h:%m:%,3s',
            buf = '2024-213T17:30:02,132',
        }, {
            fmt = '%Y-%OT%h:%m:%.3s',
            buf = '2024-213T17:30:02.132',
        }, {
            fmt = '%Y-%OT%h:%m:%s,%u',
            buf = '2024-213T17:30:02,132209',
        }, {
            fmt = '%Y-%OT%h:%m:%s.%u',
            buf = '2024-213T17:30:02.132209',
        }, {
            fmt = '%Y-%OT%hZ',
            buf = '2024-213T14Z',
        }, {
            fmt = '%Y-%OT%,1hZ',
            buf = '2024-213T14,5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%.1hZ',
            buf = '2024-213T14.5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%mZ',
            buf = '2024-213T14:30Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%,1mZ',
            buf = '2024-213T14:30,0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%.1mZ',
            buf = '2024-213T14:30.0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%m:%sZ',
            buf = '2024-213T14:30:02Z',
        }, {
            fmt = '%Y-%OT%h:%m:%.1sZ',
            buf = '2024-213T14:30:02.1Z',
        }, {
            fmt = '%Y-%OT%h:%m:%.2sZ',
            buf = '2024-213T14:30:02.13Z',
        }, {
            fmt = '%Y-%OT%h:%m:%,3sZ',
            buf = '2024-213T14:30:02,132Z',
        }, {
            fmt = '%Y-%OT%h:%m:%.3sZ',
            buf = '2024-213T14:30:02.132Z',
        }, {
            fmt = '%Y-%OT%h:%m:%s,%uZ',
            buf = '2024-213T14:30:02,132209Z',
        }, {
            fmt = '%Y-%OT%h:%m:%s.%uZ',
            buf = '2024-213T14:30:02.132209Z',
        }, {
            fmt = '%Y-%OT%h%Z',
            buf = '2024-213T17+03',
        }, {
            fmt = '%Y-%OT%,1h%Z',
            buf = '2024-213T17,5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%.1h%Z',
            buf = '2024-213T17.5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%m%Z',
            buf = '2024-213T17:30+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%,1m%Z',
            buf = '2024-213T17:30,0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%.1m%Z',
            buf = '2024-213T17:30.0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%m:%s%Z',
            buf = '2024-213T17:30:02+03',
        }, {
            fmt = '%Y-%OT%h:%m:%.1s%Z',
            buf = '2024-213T17:30:02.1+03',
        }, {
            fmt = '%Y-%OT%h:%m:%.2s%Z',
            buf = '2024-213T17:30:02.13+03',
        }, {
            fmt = '%Y-%OT%h:%m:%,3s%Z',
            buf = '2024-213T17:30:02,132+03',
        }, {
            fmt = '%Y-%OT%h:%m:%.3s%Z',
            buf = '2024-213T17:30:02.132+03',
        }, {
            fmt = '%Y-%OT%h:%m:%s,%u%Z',
            buf = '2024-213T17:30:02,132209+03',
        }, {
            fmt = '%Y-%OT%h:%m:%s.%u%Z',
            buf = '2024-213T17:30:02.132209+03',
        }, {
            fmt = '%Y-%OT%h%Z:%z',
            buf = '2024-213T17+03:00',
        }, {
            fmt = '%Y-%OT%,1h%Z:%z',
            buf = '2024-213T17,5+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%.1h%Z:%z',
            buf = '2024-213T17.5+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%m%Z:%z',
            buf = '2024-213T17:30+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%,1m%Z:%z',
            buf = '2024-213T17:30,0+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%.1m%Z:%z',
            buf = '2024-213T17:30.0+03:00',
            ts = 1722436200,
        }, {
            fmt = '%Y-%OT%h:%m:%s%Z:%z',
            buf = '2024-213T17:30:02+03:00',
        }, {
            fmt = '%Y-%OT%h:%m:%.1s%Z:%z',
            buf = '2024-213T17:30:02.1+03:00',
        }, {
            fmt = '%Y-%OT%h:%m:%.2s%Z:%z',
            buf = '2024-213T17:30:02.13+03:00',
        }, {
            fmt = '%Y-%OT%h:%m:%,3s%Z:%z',
            buf = '2024-213T17:30:02,132+03:00',
        }, {
            fmt = '%Y-%OT%h:%m:%.3s%Z:%z',
            buf = '2024-213T17:30:02.132+03:00',
        }, {
            fmt = '%Y-%OT%h:%m:%s,%u%Z:%z',
            buf = '2024-213T17:30:02,132209+03:00',
        }, {
            fmt = '%Y-%OT%h:%m:%s.%u%Z:%z',
            buf = '2024-213T17:30:02.132209+03:00',
        }, {
            fmt = '%Y%M%DT%h',
            buf = '20240731T17',
        }, {
            fmt = '%Y%M%DT%,1h',
            buf = '20240731T17,5',
            ts = 1722447000,
        }, {
            fmt = '%Y%M%DT%.1h',
            buf = '20240731T17.5',
            ts = 1722447000,
        }, {
            fmt = '%Y%M%DT%h%m',
            buf = '20240731T1730',
            ts = 1722447000,
        }, {
            fmt = '%Y%M%DT%h%,1m',
            buf = '20240731T1730,0',
            ts = 1722447000,
        }, {
            fmt = '%Y%M%DT%h%.1m',
            buf = '20240731T1730.0',
            ts = 1722447000,
        }, {
            fmt = '%Y%M%DT%h%m%s',
            buf = '20240731T173002',
        }, {
            fmt = '%Y%M%DT%h%m%.1s',
            buf = '20240731T173002.1',
        }, {
            fmt = '%Y%M%DT%h%m%.2s',
            buf = '20240731T173002.13',
        }, {
            fmt = '%Y%M%DT%h%m%,3s',
            buf = '20240731T173002,132',
        }, {
            fmt = '%Y%M%DT%h%m%.3s',
            buf = '20240731T173002.132',
        }, {
            fmt = '%Y%M%DT%h%m%s,%u',
            buf = '20240731T173002,132209',
        }, {
            fmt = '%Y%M%DT%h%m%s.%u',
            buf = '20240731T173002.132209',
        }, {
            fmt = '%Y%M%DT%hZ',
            buf = '20240731T14Z',
        }, {
            fmt = '%Y%M%DT%,1hZ',
            buf = '20240731T14,5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%.1hZ',
            buf = '20240731T14.5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%mZ',
            buf = '20240731T1430Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%,1mZ',
            buf = '20240731T1430,0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%.1mZ',
            buf = '20240731T1430.0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%m%sZ',
            buf = '20240731T143002Z',
        }, {
            fmt = '%Y%M%DT%h%m%.1sZ',
            buf = '20240731T143002.1Z',
        }, {
            fmt = '%Y%M%DT%h%m%.2sZ',
            buf = '20240731T143002.13Z',
        }, {
            fmt = '%Y%M%DT%h%m%,3sZ',
            buf = '20240731T143002,132Z',
        }, {
            fmt = '%Y%M%DT%h%m%.3sZ',
            buf = '20240731T143002.132Z',
        }, {
            fmt = '%Y%M%DT%h%m%s,%uZ',
            buf = '20240731T143002,132209Z',
        }, {
            fmt = '%Y%M%DT%h%m%s.%uZ',
            buf = '20240731T143002.132209Z',
        }, {
            fmt = '%Y%M%DT%h%Z',
            buf = '20240731T17+03',
        }, {
            fmt = '%Y%M%DT%,1h%Z',
            buf = '20240731T17,5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%.1h%Z',
            buf = '20240731T17.5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%m%Z',
            buf = '20240731T1730+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%,1m%Z',
            buf = '20240731T1730,0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%.1m%Z',
            buf = '20240731T1730.0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%m%s%Z',
            buf = '20240731T173002+03',
        }, {
            fmt = '%Y%M%DT%h%m%.1s%Z',
            buf = '20240731T173002.1+03',
        }, {
            fmt = '%Y%M%DT%h%m%.2s%Z',
            buf = '20240731T173002.13+03',
        }, {
            fmt = '%Y%M%DT%h%m%,3s%Z',
            buf = '20240731T173002,132+03',
        }, {
            fmt = '%Y%M%DT%h%m%.3s%Z',
            buf = '20240731T173002.132+03',
        }, {
            fmt = '%Y%M%DT%h%m%s,%u%Z',
            buf = '20240731T173002,132209+03',
        }, {
            fmt = '%Y%M%DT%h%m%s.%u%Z',
            buf = '20240731T173002.132209+03',
        }, {
            fmt = '%Y%M%DT%h%Z%z',
            buf = '20240731T17+0300',
        }, {
            fmt = '%Y%M%DT%,1h%Z%z',
            buf = '20240731T17,5+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%.1h%Z%z',
            buf = '20240731T17.5+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%m%Z%z',
            buf = '20240731T1730+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%,1m%Z%z',
            buf = '20240731T1730,0+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%.1m%Z%z',
            buf = '20240731T1730.0+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%M%DT%h%m%s%Z%z',
            buf = '20240731T173002+0300',
        }, {
            fmt = '%Y%M%DT%h%m%.1s%Z%z',
            buf = '20240731T173002.1+0300',
        }, {
            fmt = '%Y%M%DT%h%m%.2s%Z%z',
            buf = '20240731T173002.13+0300',
        }, {
            fmt = '%Y%M%DT%h%m%,3s%Z%z',
            buf = '20240731T173002,132+0300',
        }, {
            fmt = '%Y%M%DT%h%m%.3s%Z%z',
            buf = '20240731T173002.132+0300',
        }, {
            fmt = '%Y%M%DT%h%m%s,%u%Z%z',
            buf = '20240731T173002,132209+0300',
        }, {
            fmt = '%Y%M%DT%h%m%s.%u%Z%z',
            buf = '20240731T173002.132209+0300',
        }, {
            fmt = '%VW%W%wT%h',
            buf = '2024W313T17',
        }, {
            fmt = '%VW%W%wT%,1h',
            buf = '2024W313T17,5',
            ts = 1722447000,
        }, {
            fmt = '%VW%W%wT%.1h',
            buf = '2024W313T17.5',
            ts = 1722447000,
        }, {
            fmt = '%VW%W%wT%h%m',
            buf = '2024W313T1730',
            ts = 1722447000,
        }, {
            fmt = '%VW%W%wT%h%,1m',
            buf = '2024W313T1730,0',
            ts = 1722447000,
        }, {
            fmt = '%VW%W%wT%h%.1m',
            buf = '2024W313T1730.0',
            ts = 1722447000,
        }, {
            fmt = '%VW%W%wT%h%m%s',
            buf = '2024W313T173002',
        }, {
            fmt = '%VW%W%wT%h%m%.1s',
            buf = '2024W313T173002.1',
        }, {
            fmt = '%VW%W%wT%h%m%.2s',
            buf = '2024W313T173002.13',
        }, {
            fmt = '%VW%W%wT%h%m%,3s',
            buf = '2024W313T173002,132',
        }, {
            fmt = '%VW%W%wT%h%m%.3s',
            buf = '2024W313T173002.132',
        }, {
            fmt = '%VW%W%wT%h%m%s,%u',
            buf = '2024W313T173002,132209',
        }, {
            fmt = '%VW%W%wT%h%m%s.%u',
            buf = '2024W313T173002.132209',
        }, {
            fmt = '%VW%W%wT%hZ',
            buf = '2024W313T14Z',
        }, {
            fmt = '%VW%W%wT%,1hZ',
            buf = '2024W313T14,5Z',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%.1hZ',
            buf = '2024W313T14.5Z',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%mZ',
            buf = '2024W313T1430Z',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%,1mZ',
            buf = '2024W313T1430,0Z',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%.1mZ',
            buf = '2024W313T1430.0Z',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%m%sZ',
            buf = '2024W313T143002Z',
        }, {
            fmt = '%VW%W%wT%h%m%.1sZ',
            buf = '2024W313T143002.1Z',
        }, {
            fmt = '%VW%W%wT%h%m%.2sZ',
            buf = '2024W313T143002.13Z',
        }, {
            fmt = '%VW%W%wT%h%m%,3sZ',
            buf = '2024W313T143002,132Z',
        }, {
            fmt = '%VW%W%wT%h%m%.3sZ',
            buf = '2024W313T143002.132Z',
        }, {
            fmt = '%VW%W%wT%h%m%s,%uZ',
            buf = '2024W313T143002,132209Z',
        }, {
            fmt = '%VW%W%wT%h%m%s.%uZ',
            buf = '2024W313T143002.132209Z',
        }, {
            fmt = '%VW%W%wT%h%Z',
            buf = '2024W313T17+03',
        }, {
            fmt = '%VW%W%wT%,1h%Z',
            buf = '2024W313T17,5+03',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%.1h%Z',
            buf = '2024W313T17.5+03',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%m%Z',
            buf = '2024W313T1730+03',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%,1m%Z',
            buf = '2024W313T1730,0+03',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%.1m%Z',
            buf = '2024W313T1730.0+03',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%m%s%Z',
            buf = '2024W313T173002+03',
        }, {
            fmt = '%VW%W%wT%h%m%.1s%Z',
            buf = '2024W313T173002.1+03',
        }, {
            fmt = '%VW%W%wT%h%m%.2s%Z',
            buf = '2024W313T173002.13+03',
        }, {
            fmt = '%VW%W%wT%h%m%,3s%Z',
            buf = '2024W313T173002,132+03',
        }, {
            fmt = '%VW%W%wT%h%m%.3s%Z',
            buf = '2024W313T173002.132+03',
        }, {
            fmt = '%VW%W%wT%h%m%s,%u%Z',
            buf = '2024W313T173002,132209+03',
        }, {
            fmt = '%VW%W%wT%h%m%s.%u%Z',
            buf = '2024W313T173002.132209+03',
        }, {
            fmt = '%VW%W%wT%h%Z%z',
            buf = '2024W313T17+0300',
        }, {
            fmt = '%VW%W%wT%,1h%Z%z',
            buf = '2024W313T17,5+0300',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%.1h%Z%z',
            buf = '2024W313T17.5+0300',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%m%Z%z',
            buf = '2024W313T1730+0300',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%,1m%Z%z',
            buf = '2024W313T1730,0+0300',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%.1m%Z%z',
            buf = '2024W313T1730.0+0300',
            ts = 1722436200,
        }, {
            fmt = '%VW%W%wT%h%m%s%Z%z',
            buf = '2024W313T173002+0300',
        }, {
            fmt = '%VW%W%wT%h%m%.1s%Z%z',
            buf = '2024W313T173002.1+0300',
        }, {
            fmt = '%VW%W%wT%h%m%.2s%Z%z',
            buf = '2024W313T173002.13+0300',
        }, {
            fmt = '%VW%W%wT%h%m%,3s%Z%z',
            buf = '2024W313T173002,132+0300',
        }, {
            fmt = '%VW%W%wT%h%m%.3s%Z%z',
            buf = '2024W313T173002.132+0300',
        }, {
            fmt = '%VW%W%wT%h%m%s,%u%Z%z',
            buf = '2024W313T173002,132209+0300',
        }, {
            fmt = '%VW%W%wT%h%m%s.%u%Z%z',
            buf = '2024W313T173002.132209+0300',
        }, {
            fmt = '%Y%OT%h',
            buf = '2024213T17',
        }, {
            fmt = '%Y%OT%,1h',
            buf = '2024213T17,5',
            ts = 1722447000,
        }, {
            fmt = '%Y%OT%.1h',
            buf = '2024213T17.5',
            ts = 1722447000,
        }, {
            fmt = '%Y%OT%h%m',
            buf = '2024213T1730',
            ts = 1722447000,
        }, {
            fmt = '%Y%OT%h%,1m',
            buf = '2024213T1730,0',
            ts = 1722447000,
        }, {
            fmt = '%Y%OT%h%.1m',
            buf = '2024213T1730.0',
            ts = 1722447000,
        }, {
            fmt = '%Y%OT%h%m%s',
            buf = '2024213T173002',
        }, {
            fmt = '%Y%OT%h%m%.1s',
            buf = '2024213T173002.1',
        }, {
            fmt = '%Y%OT%h%m%.2s',
            buf = '2024213T173002.13',
        }, {
            fmt = '%Y%OT%h%m%,3s',
            buf = '2024213T173002,132',
        }, {
            fmt = '%Y%OT%h%m%.3s',
            buf = '2024213T173002.132',
        }, {
            fmt = '%Y%OT%h%m%s,%u',
            buf = '2024213T173002,132209',
        }, {
            fmt = '%Y%OT%h%m%s.%u',
            buf = '2024213T173002.132209',
        }, {
            fmt = '%Y%OT%hZ',
            buf = '2024213T14Z',
        }, {
            fmt = '%Y%OT%,1hZ',
            buf = '2024213T14,5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%.1hZ',
            buf = '2024213T14.5Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%mZ',
            buf = '2024213T1430Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%,1mZ',
            buf = '2024213T1430,0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%.1mZ',
            buf = '2024213T1430.0Z',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%m%sZ',
            buf = '2024213T143002Z',
        }, {
            fmt = '%Y%OT%h%m%.1sZ',
            buf = '2024213T143002.1Z',
        }, {
            fmt = '%Y%OT%h%m%.2sZ',
            buf = '2024213T143002.13Z',
        }, {
            fmt = '%Y%OT%h%m%,3sZ',
            buf = '2024213T143002,132Z',
        }, {
            fmt = '%Y%OT%h%m%.3sZ',
            buf = '2024213T143002.132Z',
        }, {
            fmt = '%Y%OT%h%m%s,%uZ',
            buf = '2024213T143002,132209Z',
        }, {
            fmt = '%Y%OT%h%m%s.%uZ',
            buf = '2024213T143002.132209Z',
        }, {
            fmt = '%Y%OT%h%Z',
            buf = '2024213T17+03',
        }, {
            fmt = '%Y%OT%,1h%Z',
            buf = '2024213T17,5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%.1h%Z',
            buf = '2024213T17.5+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%m%Z',
            buf = '2024213T1730+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%,1m%Z',
            buf = '2024213T1730,0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%.1m%Z',
            buf = '2024213T1730.0+03',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%m%s%Z',
            buf = '2024213T173002+03',
        }, {
            fmt = '%Y%OT%h%m%.1s%Z',
            buf = '2024213T173002.1+03',
        }, {
            fmt = '%Y%OT%h%m%.2s%Z',
            buf = '2024213T173002.13+03',
        }, {
            fmt = '%Y%OT%h%m%,3s%Z',
            buf = '2024213T173002,132+03',
        }, {
            fmt = '%Y%OT%h%m%.3s%Z',
            buf = '2024213T173002.132+03',
        }, {
            fmt = '%Y%OT%h%m%s,%u%Z',
            buf = '2024213T173002,132209+03',
        }, {
            fmt = '%Y%OT%h%m%s.%u%Z',
            buf = '2024213T173002.132209+03',
        }, {
            fmt = '%Y%OT%h%Z%z',
            buf = '2024213T17+0300',
        }, {
            fmt = '%Y%OT%,1h%Z%z',
            buf = '2024213T17,5+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%.1h%Z%z',
            buf = '2024213T17.5+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%m%Z%z',
            buf = '2024213T1730+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%,1m%Z%z',
            buf = '2024213T1730,0+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%.1m%Z%z',
            buf = '2024213T1730.0+0300',
            ts = 1722436200,
        }, {
            fmt = '%Y%OT%h%m%s%Z%z',
            buf = '2024213T173002+0300',
        }, {
            fmt = '%Y%OT%h%m%.1s%Z%z',
            buf = '2024213T173002.1+0300',
        }, {
            fmt = '%Y%OT%h%m%.2s%Z%z',
            buf = '2024213T173002.13+0300',
        }, {
            fmt = '%Y%OT%h%m%,3s%Z%z',
            buf = '2024213T173002,132+0300',
        }, {
            fmt = '%Y%OT%h%m%.3s%Z%z',
            buf = '2024213T173002.132+0300',
        }, {
            fmt = '%Y%OT%h%m%s,%u%Z%z',
            buf = '2024213T173002,132209+0300',
        }, {
            fmt = '%Y%OT%h%m%s.%u%Z%z',
            buf = '2024213T173002.132209+0300',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s+08',
            buf = '2024-07-31T22:30:02+08',
        }, {
            fmt = '%Y-%M-%DT%h-12',
            buf = '2024-07-31T02-12',
        }, {
            fmt = '%Y-%M-%DT%h-12:00',
            buf = '2024-07-31T02-12:00',
        }, {
            fmt = '%Y-%M-%DT%h:%m-12',
            buf = '2024-07-31T02:30-12',
        }, {
            fmt = '%Y-%M-%DT%h:%m-12:00',
            buf = '2024-07-31T02:30-12:00',
        },
        -- Ranges.
        {
            fmt = '%Y-%M-%DT%h/P1DT1H',
            buf = '2024-07-31T17/P1DT1H',
        }, {
            fmt = '%Y-%M-%DT%h:%m/P1DT1H',
            buf = '2024-07-31T17:42/P1DT1H',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%s/P1DT1H',
            buf = '2024-07-31T17:42:56/P1DT1H',
        }, {
            fmt = '%Y-%M-%DT%h:%m:%.3s/P1DT1H',
            buf = '2024-07-31T17:42:56.132/P1DT1H',
        }, {
            fmt = '%V-W%W-%wT%h/P1DT1H',
            buf = '2024-W31-3T17/P1DT1H',
        }, {
            fmt = '%V-W%W-%wT%h:%m/P1DT1H',
            buf = '2024-W31-3T17:42/P1DT1H',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%s/P1DT1H',
            buf = '2024-W31-3T17:42:56/P1DT1H',
        }, {
            fmt = '%V-W%W-%wT%h:%m:%.3s/P1DT1H',
            buf = '2024-W31-3T17:42:56.132/P1DT1H',
        }, {
            fmt = '%Y-%OT%h/P1DT1H',
            buf = '2024-213T17/P1DT1H',
        }, {
            fmt = '%Y-%OT%h:%m/P1DT1H',
            buf = '2024-213T17:42/P1DT1H',
        }, {
            fmt = '%Y-%OT%h:%m:%s/P1DT1H',
            buf = '2024-213T17:42:56/P1DT1H',
        }, {
            fmt = '%Y-%OT%h:%m:%.3s/P1DT1H',
            buf = '2024-213T17:42:56.132/P1DT1H',
        },
    },
}

local UNSUPPORTED_DATETIME_FORMATS = {
    ['RFC3339 AND ISO8601'] = {
        -- Times.
        {
            fmt = '%h:%m:%s%Z:%z',
            buf = '15:55:43+03:00',
        }, {
            fmt = '%h:%m:%.1s%Z:%z',
            buf = '15:55:43.1+03:00',
        }, {
            fmt = '%h:%m:%.2s%Z:%z',
            buf = '15:55:43.13+03:00',
        }, {
            fmt = '%h:%m:%.3s%Z:%z',
            buf = '15:55:43.132+03:00',
        }, {
            fmt = '%h:%m:%s.%u%Z:%z',
            buf = '15:55:43.132209+03:00',
        }, {
            fmt = '%h:%m:%sZ',
            buf = '12:55:43Z',
        }, {
            fmt = '%h:%m:%.1sZ',
            buf = '12:55:43.1Z',
        }, {
            fmt = '%h:%m:%.2sZ',
            buf = '12:55:43.13Z',
        }, {
            fmt = '%h:%m:%.3sZ',
            buf = '12:55:43.132Z',
        }, {
            fmt = '%h:%m:%s.%uZ',
            buf = '12:55:43.132209Z',
        }, {
            fmt = '%h:%m:%s+00:00',
            buf = '12:55:43+00:00',
        }, {
            fmt = '%h:%m:%.1s+00:00',
            buf = '12:55:43.1+00:00',
        }, {
            fmt = '%h:%m:%.3s+00:00',
            buf = '12:55:43.132+00:00',
        }, {
            fmt = '%h:%m:%s.%u+00:00',
            buf = '12:55:43.132209+00:00',
        },
    },

    ['RFC3339 ONLY'] = {
        -- Times.
        {
            fmt = '%h:%m:%s-00:00',
            buf = '12:55:43-00:00',
        }, {
            fmt = '%h:%m:%.1s-00:00',
            buf = '12:55:43.1-00:00',
        }, {
            fmt = '%h:%m:%.3s-00:00',
            buf = '12:55:43.132-00:00',
        }, {
            fmt = '%h:%m:%s.%u-00:00',
            buf = '12:55:43.132209-00:00',
        },
        -- Dates-Times.
        {
            fmt = '%Y-%M-%D_%h:%m:%sZ',
            buf = '2024-07-31_14:30:02Z',
        }, {
            fmt = '%Y-%M-%D_%h:%m:%sz',
            buf = '2024-07-31_14:30:02z',
        }, {
            fmt = '%Y-%M-%D_%h:%m:%.3sZ',
            buf = '2024-07-31_14:30:02.132Z',
        }, {
            fmt = '%Y-%M-%D_%h:%m:%s.%uZ',
            buf = '2024-07-31_14:30:02.132209Z',
        }, {
            fmt = '%Y-%M-%D_%h:%m:%.3sz',
            buf = '2024-07-31_14:30:02.132z',
        }, {
            fmt = '%Y-%M-%D_%h:%m:%s.%uz',
            buf = '2024-07-31_14:30:02.132209z',
        },
    },

    ['ISO8601 ONLY'] = {
        -- Dates.
        {
            fmt = '%C',
            buf = '20',
        }, {
            fmt = '%X',
            buf = '202',
        }, {
            fmt = '%Y',
            buf = '2024',
        }, {
            fmt = '%Y-%M',
            buf = '2024-07',
        }, {
            fmt = '%V-W%W',
            buf = '2024-W31',
        }, {
            fmt = '%VW%W',
            buf = '2024W31',
        },
        -- Times.
        {
            fmt = '%h',
            buf = '15',
        }, {
            fmt = '%,1h',
            buf = '15,9',
        }, {
            fmt = '%.1h',
            buf = '15.9',
        }, {
            fmt = '%h:%m',
            buf = '15:55',
        }, {
            fmt = '%h:%,1m',
            buf = '15:55,7',
        }, {
            fmt = '%h:%.1m',
            buf = '15:55.7',
        }, {
            fmt = '%h:%m:%s',
            buf = '15:55:43',
        }, {
            fmt = '%h:%m:%.1s',
            buf = '15:55:43.1',
        }, {
            fmt = '%h:%m:%.2s',
            buf = '15:55:43.13',
        }, {
            fmt = '%h:%m:%,3s',
            buf = '15:55:43,132',
        }, {
            fmt = '%h:%m:%.3s',
            buf = '15:55:43.132',
        }, {
            fmt = '%h:%m:%s,%u',
            buf = '15:55:43,132209',
        }, {
            fmt = '%h:%m:%s.%u',
            buf = '15:55:43.132209',
        }, {
            fmt = '%hZ',
            buf = '12Z',
        }, {
            fmt = '%,1hZ',
            buf = '12,9Z',
        }, {
            fmt = '%.1hZ',
            buf = '12.9Z',
        }, {
            fmt = '%h:%mZ',
            buf = '12:55Z',
        }, {
            fmt = '%h:%,1mZ',
            buf = '12:55,7Z',
        }, {
            fmt = '%h:%.1mZ',
            buf = '12:55.7Z',
        }, {
            fmt = '%h:%m:%,3sZ',
            buf = '12:55:43,132Z',
        }, {
            fmt = '%h:%m:%s,%uZ',
            buf = '12:55:43,132209Z',
        }, {
            fmt = '%h%Z',
            buf = '15+03',
        }, {
            fmt = '%,1h%Z',
            buf = '15,9+03',
        }, {
            fmt = '%.1h%Z',
            buf = '15.9+03',
        }, {
            fmt = '%h:%m%Z',
            buf = '15:55+03',
        }, {
            fmt = '%h:%,1m%Z',
            buf = '15:55,7+03',
        }, {
            fmt = '%h:%.1m%Z',
            buf = '15:55.7+03',
        }, {
            fmt = '%h:%m:%s%Z',
            buf = '15:55:43+03',
        }, {
            fmt = '%h:%m:%.1s%Z',
            buf = '15:55:43.1+03',
        }, {
            fmt = '%h:%m:%.2s%Z',
            buf = '15:55:43.13+03',
        }, {
            fmt = '%h:%m:%,3s%Z',
            buf = '15:55:43,132+03',
        }, {
            fmt = '%h:%m:%.3s%Z',
            buf = '15:55:43.132+03',
        }, {
            fmt = '%h:%m:%s,%u%Z',
            buf = '15:55:43,132209+03',
        }, {
            fmt = '%h:%m:%s.%u%Z',
            buf = '15:55:43.132209+03',
        }, {
            fmt = '%h%Z:%z',
            buf = '15+03:00',
        }, {
            fmt = '%,1h%Z:%z',
            buf = '15,9+03:00',
        }, {
            fmt = '%.1h%Z:%z',
            buf = '15.9+03:00',
        }, {
            fmt = '%h:%m%Z:%z',
            buf = '15:55+03:00',
        }, {
            fmt = '%h:%,1m%Z:%z',
            buf = '15:55,7+03:00',
        }, {
            fmt = '%h:%.1m%Z:%z',
            buf = '15:55.7+03:00',
        }, {
            fmt = '%h:%m:%,3s%Z:%z',
            buf = '15:55:43,132+03:00',
        }, {
            fmt = '%h:%m:%s,%u%Z:%z',
            buf = '15:55:43,132209+03:00',
        }, {
            fmt = 'T%h',
            buf = 'T15',
        }, {
            fmt = 'T%,1h',
            buf = 'T15,9',
        }, {
            fmt = 'T%.1h',
            buf = 'T15.9',
        }, {
            fmt = 'T%h:%m',
            buf = 'T15:55',
        }, {
            fmt = 'T%h:%,1m',
            buf = 'T15:55,7',
        }, {
            fmt = 'T%h:%.1m',
            buf = 'T15:55.7',
        }, {
            fmt = 'T%h:%m:%s',
            buf = 'T15:55:43',
        }, {
            fmt = 'T%h:%m:%.1s',
            buf = 'T15:55:43.1',
        }, {
            fmt = 'T%h:%m:%.2s',
            buf = 'T15:55:43.13',
        }, {
            fmt = 'T%h:%m:%,3s',
            buf = 'T15:55:43,132',
        }, {
            fmt = 'T%h:%m:%.3s',
            buf = 'T15:55:43.132',
        }, {
            fmt = 'T%h:%m:%s,%u',
            buf = 'T15:55:43,132209',
        }, {
            fmt = 'T%h:%m:%s.%u',
            buf = 'T15:55:43.132209',
        }, {
            fmt = 'T%hZ',
            buf = 'T12Z',
        }, {
            fmt = 'T%,1hZ',
            buf = 'T12,9Z',
        }, {
            fmt = 'T%.1hZ',
            buf = 'T12.9Z',
        }, {
            fmt = 'T%h:%mZ',
            buf = 'T12:55Z',
        }, {
            fmt = 'T%h:%,1mZ',
            buf = 'T12:55,7Z',
        }, {
            fmt = 'T%h:%.1mZ',
            buf = 'T12:55.7Z',
        }, {
            fmt = 'T%h:%m:%sZ',
            buf = 'T12:55:43Z',
        }, {
            fmt = 'T%h:%m:%.1sZ',
            buf = 'T12:55:43.1Z',
        }, {
            fmt = 'T%h:%m:%.2sZ',
            buf = 'T12:55:43.13Z',
        }, {
            fmt = 'T%h:%m:%,3sZ',
            buf = 'T12:55:43,132Z',
        }, {
            fmt = 'T%h:%m:%.3sZ',
            buf = 'T12:55:43.132Z',
        }, {
            fmt = 'T%h:%m:%s,%uZ',
            buf = 'T12:55:43,132209Z',
        }, {
            fmt = 'T%h:%m:%s.%uZ',
            buf = 'T12:55:43.132209Z',
        }, {
            fmt = 'T%h%Z',
            buf = 'T15+03',
        }, {
            fmt = 'T%,1h%Z',
            buf = 'T15,9+03',
        }, {
            fmt = 'T%.1h%Z',
            buf = 'T15.9+03',
        }, {
            fmt = 'T%h:%m%Z',
            buf = 'T15:55+03',
        }, {
            fmt = 'T%h:%,1m%Z',
            buf = 'T15:55,7+03',
        }, {
            fmt = 'T%h:%.1m%Z',
            buf = 'T15:55.7+03',
        }, {
            fmt = 'T%h:%m:%s%Z',
            buf = 'T15:55:43+03',
        }, {
            fmt = 'T%h:%m:%.1s%Z',
            buf = 'T15:55:43.1+03',
        }, {
            fmt = 'T%h:%m:%.2s%Z',
            buf = 'T15:55:43.13+03',
        }, {
            fmt = 'T%h:%m:%,3s%Z',
            buf = 'T15:55:43,132+03',
        }, {
            fmt = 'T%h:%m:%.3s%Z',
            buf = 'T15:55:43.132+03',
        }, {
            fmt = 'T%h:%m:%s,%u%Z',
            buf = 'T15:55:43,132209+03',
        }, {
            fmt = 'T%h:%m:%s.%u%Z',
            buf = 'T15:55:43.132209+03',
        }, {
            fmt = 'T%h%Z:%z',
            buf = 'T15+03:00',
        }, {
            fmt = 'T%,1h%Z:%z',
            buf = 'T15,9+03:00',
        }, {
            fmt = 'T%.1h%Z:%z',
            buf = 'T15.9+03:00',
        }, {
            fmt = 'T%h:%m%Z:%z',
            buf = 'T15:55+03:00',
        }, {
            fmt = 'T%h:%,1m%Z:%z',
            buf = 'T15:55,7+03:00',
        }, {
            fmt = 'T%h:%.1m%Z:%z',
            buf = 'T15:55.7+03:00',
        }, {
            fmt = 'T%h:%m:%s%Z:%z',
            buf = 'T15:55:43+03:00',
        }, {
            fmt = 'T%h:%m:%.1s%Z:%z',
            buf = 'T15:55:43.1+03:00',
        }, {
            fmt = 'T%h:%m:%.2s%Z:%z',
            buf = 'T15:55:43.13+03:00',
        }, {
            fmt = 'T%h:%m:%,3s%Z:%z',
            buf = 'T15:55:43,132+03:00',
        }, {
            fmt = 'T%h:%m:%.3s%Z:%z',
            buf = 'T15:55:43.132+03:00',
        }, {
            fmt = 'T%h:%m:%s,%u%Z:%z',
            buf = 'T15:55:43,132209+03:00',
        }, {
            fmt = 'T%h:%m:%s.%u%Z:%z',
            buf = 'T15:55:43.132209+03:00',
        }, {
            fmt = '%h%m',
            buf = '1555',
        }, {
            fmt = '%h%,1m',
            buf = '1555,7',
        }, {
            fmt = '%h%.1m',
            buf = '1555.7',
        }, {
            fmt = '%h%m%s',
            buf = '155543',
        }, {
            fmt = '%h%m%.1s',
            buf = '155543.1',
        }, {
            fmt = '%h%m%.2s',
            buf = '155543.13',
        }, {
            fmt = '%h%m%,3s',
            buf = '155543,132',
        }, {
            fmt = '%h%m%.3s',
            buf = '155543.132',
        }, {
            fmt = '%h%m%s,%u',
            buf = '155543,132209',
        }, {
            fmt = '%h%m%s.%u',
            buf = '155543.132209',
        }, {
            fmt = '%h%mZ',
            buf = '1255Z',
        }, {
            fmt = '%h%,1mZ',
            buf = '1255,7Z',
        }, {
            fmt = '%h%.1mZ',
            buf = '1255.7Z',
        }, {
            fmt = '%h%m%sZ',
            buf = '125543Z',
        }, {
            fmt = '%h%m%.1sZ',
            buf = '125543.1Z',
        }, {
            fmt = '%h%m%.2sZ',
            buf = '125543.13Z',
        }, {
            fmt = '%h%m%,3sZ',
            buf = '125543,132Z',
        }, {
            fmt = '%h%m%.3sZ',
            buf = '125543.132Z',
        }, {
            fmt = '%h%m%s,%uZ',
            buf = '125543,132209Z',
        }, {
            fmt = '%h%m%s.%uZ',
            buf = '125543.132209Z',
        }, {
            fmt = '%h%m%Z',
            buf = '1555+03',
        }, {
            fmt = '%h%,1m%Z',
            buf = '1555,7+03',
        }, {
            fmt = '%h%.1m%Z',
            buf = '1555.7+03',
        }, {
            fmt = '%h%m%s%Z',
            buf = '155543+03',
        }, {
            fmt = '%h%m%.1s%Z',
            buf = '155543.1+03',
        }, {
            fmt = '%h%m%.2s%Z',
            buf = '155543.13+03',
        }, {
            fmt = '%h%m%,3s%Z',
            buf = '155543,132+03',
        }, {
            fmt = '%h%m%.3s%Z',
            buf = '155543.132+03',
        }, {
            fmt = '%h%m%s,%u%Z',
            buf = '155543,132209+03',
        }, {
            fmt = '%h%m%s.%u%Z',
            buf = '155543.132209+03',
        }, {
            fmt = '%h%Z%z',
            buf = '15+0300',
        }, {
            fmt = '%,1h%Z%z',
            buf = '15,9+0300',
        }, {
            fmt = '%.1h%Z%z',
            buf = '15.9+0300',
        }, {
            fmt = '%h%m%Z%z',
            buf = '1555+0300',
        }, {
            fmt = '%h%,1m%Z%z',
            buf = '1555,7+0300',
        }, {
            fmt = '%h%.1m%Z%z',
            buf = '1555.7+0300',
        }, {
            fmt = '%h%m%s%Z%z',
            buf = '155543+0300',
        }, {
            fmt = '%h%m%.1s%Z%z',
            buf = '155543.1+0300',
        }, {
            fmt = '%h%m%.2s%Z%z',
            buf = '155543.13+0300',
        }, {
            fmt = '%h%m%,3s%Z%z',
            buf = '155543,132+0300',
        }, {
            fmt = '%h%m%.3s%Z%z',
            buf = '155543.132+0300',
        }, {
            fmt = '%h%m%s,%u%Z%z',
            buf = '155543,132209+0300',
        }, {
            fmt = '%h%m%s.%u%Z%z',
            buf = '155543.132209+0300',
        }, {
            fmt = 'T%h%m',
            buf = 'T1555',
        }, {
            fmt = 'T%h%,1m',
            buf = 'T1555,7',
        }, {
            fmt = 'T%h%.1m',
            buf = 'T1555.7',
        }, {
            fmt = 'T%h%m%s',
            buf = 'T155543',
        }, {
            fmt = 'T%h%m%.1s',
            buf = 'T155543.1',
        }, {
            fmt = 'T%h%m%.2s',
            buf = 'T155543.13',
        }, {
            fmt = 'T%h%m%,3s',
            buf = 'T155543,132',
        }, {
            fmt = 'T%h%m%.3s',
            buf = 'T155543.132',
        }, {
            fmt = 'T%h%m%s,%u',
            buf = 'T155543,132209',
        }, {
            fmt = 'T%h%m%s.%u',
            buf = 'T155543.132209',
        }, {
            fmt = 'T%h%mZ',
            buf = 'T1255Z',
        }, {
            fmt = 'T%h%,1mZ',
            buf = 'T1255,7Z',
        }, {
            fmt = 'T%h%.1mZ',
            buf = 'T1255.7Z',
        }, {
            fmt = 'T%h%m%sZ',
            buf = 'T125543Z',
        }, {
            fmt = 'T%h%m%.1sZ',
            buf = 'T125543.1Z',
        }, {
            fmt = 'T%h%m%.2sZ',
            buf = 'T125543.13Z',
        }, {
            fmt = 'T%h%m%,3sZ',
            buf = 'T125543,132Z',
        }, {
            fmt = 'T%h%m%.3sZ',
            buf = 'T125543.132Z',
        }, {
            fmt = 'T%h%m%s,%uZ',
            buf = 'T125543,132209Z',
        }, {
            fmt = 'T%h%m%s.%uZ',
            buf = 'T125543.132209Z',
        }, {
            fmt = 'T%h%m%Z',
            buf = 'T1555+03',
        }, {
            fmt = 'T%h%,1m%Z',
            buf = 'T1555,7+03',
        }, {
            fmt = 'T%h%.1m%Z',
            buf = 'T1555.7+03',
        }, {
            fmt = 'T%h%m%s%Z',
            buf = 'T155543+03',
        }, {
            fmt = 'T%h%m%.1s%Z',
            buf = 'T155543.1+03',
        }, {
            fmt = 'T%h%m%.2s%Z',
            buf = 'T155543.13+03',
        }, {
            fmt = 'T%h%m%,3s%Z',
            buf = 'T155543,132+03',
        }, {
            fmt = 'T%h%m%.3s%Z',
            buf = 'T155543.132+03',
        }, {
            fmt = 'T%h%m%s,%u%Z',
            buf = 'T155543,132209+03',
        }, {
            fmt = 'T%h%m%s.%u%Z',
            buf = 'T155543.132209+03',
        }, {
            fmt = 'T%h%Z%z',
            buf = 'T15+0300',
        }, {
            fmt = 'T%,1h%Z%z',
            buf = 'T15,9+0300',
        }, {
            fmt = 'T%.1h%Z%z',
            buf = 'T15.9+0300',
        }, {
            fmt = 'T%h%m%Z%z',
            buf = 'T1555+0300',
        }, {
            fmt = 'T%h%,1m%Z%z',
            buf = 'T1555,7+0300',
        }, {
            fmt = 'T%h%.1m%Z%z',
            buf = 'T1555.7+0300',
        }, {
            fmt = 'T%h%m%s%Z%z',
            buf = 'T155543+0300',
        }, {
            fmt = 'T%h%m%.1s%Z%z',
            buf = 'T155543.1+0300',
        }, {
            fmt = 'T%h%m%.2s%Z%z',
            buf = 'T155543.13+0300',
        }, {
            fmt = 'T%h%m%,3s%Z%z',
            buf = 'T155543,132+0300',
        }, {
            fmt = 'T%h%m%.3s%Z%z',
            buf = 'T155543.132+0300',
        }, {
            fmt = 'T%h%m%s,%u%Z%z',
            buf = 'T155543,132209+0300',
        }, {
            fmt = 'T%h%m%s.%u%Z%z',
            buf = 'T155543.132209+0300',
        },
        -- Periods.
        {
            fmt = 'P1Y',
            buf = 'P1Y',
        }, {
            fmt = 'P1,5Y',
            buf = 'P1,5Y',
        }, {
            fmt = 'P1.5Y',
            buf = 'P1.5Y',
        }, {
            fmt = 'P1M',
            buf = 'P1M',
        }, {
            fmt = 'P1W',
            buf = 'P1W',
        }, {
            fmt = 'P1D',
            buf = 'P1D',
        }, {
            fmt = 'PT1H',
            buf = 'PT1H',
        }, {
            fmt = 'P1H',
            buf = 'P1H',
        }, {
            fmt = 'PT1M',
            buf = 'PT1M',
        }, {
            fmt = 'PT1S',
            buf = 'PT1S',
        }, {
            fmt = 'P1S',
            buf = 'P1S',
        }, {
            fmt = 'PT1,5S',
            buf = 'PT1,5S',
        }, {
            fmt = 'PT1.5S',
            buf = 'PT1.5S',
        }, {
            fmt = 'P1Y1M',
            buf = 'P1Y1M',
        }, {
            fmt = 'P1Y1D',
            buf = 'P1Y1D',
        }, {
            fmt = 'P1Y1M1D',
            buf = 'P1Y1M1D',
        }, {
            fmt = 'P1Y1M1DT1H1M1S',
            buf = 'P1Y1M1DT1H1M1S',
        }, {
            fmt = 'P1DT1H',
            buf = 'P1DT1H',
        }, {
            fmt = 'P1MT1M',
            buf = 'P1MT1M',
        }, {
            fmt = 'P1DT1M',
            buf = 'P1DT1M',
        }, {
            fmt = 'P1.5W',
            buf = 'P1.5W',
        }, {
            fmt = 'P1,5W',
            buf = 'P1,5W',
        }, {
            fmt = 'P1DT1.000S',
            buf = 'P1DT1.000S',
        }, {
            fmt = 'P1DT1.00000S',
            buf = 'P1DT1.00000S',
        }, {
            fmt = 'P1DT1H1M1.1S',
            buf = 'P1DT1H1M1.1S',
        }, {
            fmt = 'P1H1M1.1S',
            buf = 'P1H1M1.1S',
        },
        -- Ranges.
        {
            fmt = '%Y-%M-%D/P1Y',
            buf = '2024-07-31/P1Y',
        }, {
            fmt = '%Y-%M-%D/P1M',
            buf = '2024-07-31/P1M',
        }, {
            fmt = '%Y-%M-%D/P1D',
            buf = '2024-07-31/P1D',
        }, {
            fmt = '%V-W%W-%w/P1Y',
            buf = '2024-W31-3/P1Y',
        }, {
            fmt = '%V-W%W-%w/P1M',
            buf = '2024-W31-3/P1M',
        }, {
            fmt = '%V-W%W-%w/P1D',
            buf = '2024-W31-3/P1D',
        }, {
            fmt = '%Y-%O/P1Y',
            buf = '2024-213/P1Y',
        }, {
            fmt = '%Y-%O/P1M',
            buf = '2024-213/P1M',
        }, {
            fmt = '%Y-%O/P1D',
            buf = '2024-213/P1D',
        }, {
            fmt = '%Y-%M-%D/%Y-%M-%D',
            buf = '2024-07-31/2024-07-31',
        }, {
            fmt = '%Y-%M-%D/%V-W%W-%w',
            buf = '2024-07-31/2024-W31-3',
        }, {
            fmt = '%Y-%M-%D/%Y-%O',
            buf = '2024-07-31/2024-213',
        }, {
            fmt = '%V-W%W-%w/%Y-%M-%D',
            buf = '2024-W31-3/2024-07-31',
        }, {
            fmt = '%V-W%W-%w/%V-W%W-%w',
            buf = '2024-W31-3/2024-W31-3',
        }, {
            fmt = '%V-W%W-%w/%Y-%O',
            buf = '2024-W31-3/2024-213',
        }, {
            fmt = '%Y-%O/%Y-%M-%D',
            buf = '2024-213/2024-07-31',
        }, {
            fmt = '%Y-%O/%V-W%W-%w',
            buf = '2024-213/2024-W31-3',
        }, {
            fmt = '%Y-%O/%Y-%O',
            buf = '2024-213/2024-213',
        }, {
            fmt = 'P1Y/%Y-%M-%D',
            buf = 'P1Y/2024-07-31',
        }, {
            fmt = 'P1Y/%V-W%W-%w',
            buf = 'P1Y/2024-W31-3',
        }, {
            fmt = 'P1Y/%Y-%O',
            buf = 'P1Y/2024-213',
        }, {
            fmt = 'P1M/%Y-%M-%D',
            buf = 'P1M/2024-07-31',
        }, {
            fmt = 'P1M/%V-W%W-%w',
            buf = 'P1M/2024-W31-3',
        }, {
            fmt = 'P1M/%Y-%O',
            buf = 'P1M/2024-213',
        }, {
            fmt = 'P1D/%Y-%M-%D',
            buf = 'P1D/2024-07-31',
        }, {
            fmt = 'P1D/%V-W%W-%w',
            buf = 'P1D/2024-W31-3',
        }, {
            fmt = 'P1D/%Y-%O',
            buf = 'P1D/2024-213',
        }, {
            fmt = '%Y-%M-%DT%h:%mZ/P1DT1H',
            buf = '2024-07-31T17:42Z/P1DT1H',
        }, {
            fmt = '%V-W%W-%wT%h:%mZ/P1DT1H',
            buf = '2024-W31-3T17:42Z/P1DT1H',
        }, {
            fmt = '%Y-%OT%h:%mZ/P1DT1H',
            buf = '2024-213T17:42Z/P1DT1H',
        }, {
            fmt = 'P1DT1H/%Y-%M-%DT%h',
            buf = 'P1DT1H/2024-07-31T17',
        }, {
            fmt = 'P1DT1H/%Y-%M-%DT%h:%m',
            buf = 'P1DT1H/2024-07-31T17:42',
        }, {
            fmt = 'P1DT1H/%Y-%M-%DT%h:%m:%s',
            buf = 'P1DT1H/2024-07-31T17:42:56',
        }, {
            fmt = 'P1DT1H/%Y-%M-%DT%h:%m:%.3s',
            buf = 'P1DT1H/2024-07-31T17:42:56.132',
        }, {
            fmt = 'P1DT1H/%Y-%M-%DT%h:%mZ',
            buf = 'P1DT1H/2024-07-31T14:42Z',
        }, {
            fmt = 'P1DT1H/%V-W%W-%wT%h',
            buf = 'P1DT1H/2024-W31-3T17',
        }, {
            fmt = 'P1DT1H/%V-W%W-%wT%h:%m',
            buf = 'P1DT1H/2024-W31-3T17:42',
        }, {
            fmt = 'P1DT1H/%V-W%W-%wT%h:%m:%s',
            buf = 'P1DT1H/2024-W31-3T17:42:56',
        }, {
            fmt = 'P1DT1H/%V-W%W-%wT%h:%m:%.3s',
            buf = 'P1DT1H/2024-W31-3T17:42:56.132',
        }, {
            fmt = 'P1DT1H/%V-W%W-%wT%h:%mZ',
            buf = 'P1DT1H/2024-W31-3T14:42Z',
        }, {
            fmt = 'P1DT1H/%Y-%OT%h',
            buf = 'P1DT1H/2024-213T17',
        }, {
            fmt = 'P1DT1H/%Y-%OT%h:%m',
            buf = 'P1DT1H/2024-213T17:42',
        }, {
            fmt = 'P1DT1H/%Y-%OT%h:%m:%s',
            buf = 'P1DT1H/2024-213T17:42:56',
        }, {
            fmt = 'P1DT1H/%Y-%OT%h:%m:%.3s',
            buf = 'P1DT1H/2024-213T17:42:56.132',
        }, {
            fmt = 'P1DT1H/%Y-%OT%h:%mZ',
            buf = 'P1DT1H/2024-213T14:42Z',
        }, {
            fmt = 'R/%Y-%M-%D/P1Y',
            buf = 'R/2024-07-31/P1Y',
        }, {
            fmt = 'R/%V-W%W-%w/P1Y',
            buf = 'R/2024-W31-3/P1Y',
        }, {
            fmt = 'R/%Y-%O/P1Y',
            buf = 'R/2024-213/P1Y',
        }, {
            fmt = 'R/%Y-%M-%D/%Y-%M-%D',
            buf = 'R/2024-07-31/2024-07-31',
        }, {
            fmt = 'R/%Y-%M-%D/%V-W%W-%w',
            buf = 'R/2024-07-31/2024-W31-3',
        }, {
            fmt = 'R/%Y-%M-%D/%Y-%O',
            buf = 'R/2024-07-31/2024-213',
        }, {
            fmt = 'R/%V-W%W-%w/%Y-%M-%D',
            buf = 'R/2024-W31-3/2024-07-31',
        }, {
            fmt = 'R/%V-W%W-%w/%V-W%W-%w',
            buf = 'R/2024-W31-3/2024-W31-3',
        }, {
            fmt = 'R/%V-W%W-%w/%Y-%O',
            buf = 'R/2024-W31-3/2024-213',
        }, {
            fmt = 'R/%Y-%O/%Y-%M-%D',
            buf = 'R/2024-213/2024-07-31',
        }, {
            fmt = 'R/%Y-%O/%V-W%W-%w',
            buf = 'R/2024-213/2024-W31-3',
        }, {
            fmt = 'R/%Y-%O/%Y-%O',
            buf = 'R/2024-213/2024-213',
        }, {
            fmt = 'R10/%Y-%M-%D/P1Y',
            buf = 'R10/2024-07-31/P1Y',
        }, {
            fmt = 'R10/%V-W%W-%w/P1Y',
            buf = 'R10/2024-W31-3/P1Y',
        }, {
            fmt = 'R10/%Y-%O/P1Y',
            buf = 'R10/2024-213/P1Y',
        }, {
            fmt = 'R10/%Y-%M-%D/%Y-%M-%D',
            buf = 'R10/2024-07-31/2024-07-31',
        }, {
            fmt = 'R10/%Y-%M-%D/%V-W%W-%w',
            buf = 'R10/2024-07-31/2024-W31-3',
        }, {
            fmt = 'R10/%Y-%M-%D/%Y-%O',
            buf = 'R10/2024-07-31/2024-213',
        }, {
            fmt = 'R10/%V-W%W-%w/%Y-%M-%D',
            buf = 'R10/2024-W31-3/2024-07-31',
        }, {
            fmt = 'R10/%V-W%W-%w/%V-W%W-%w',
            buf = 'R10/2024-W31-3/2024-W31-3',
        }, {
            fmt = 'R10/%V-W%W-%w/%Y-%O',
            buf = 'R10/2024-W31-3/2024-213',
        }, {
            fmt = 'R10/%Y-%O/%Y-%M-%D',
            buf = 'R10/2024-213/2024-07-31',
        }, {
            fmt = 'R10/%Y-%O/%V-W%W-%w',
            buf = 'R10/2024-213/2024-W31-3',
        }, {
            fmt = 'R10/%Y-%O/%Y-%O',
            buf = 'R10/2024-213/2024-213',
        },
    }

}

local pg = t.group('pgroup')

-- XXX: It is not possible to use parameterization by passing a
-- table with test parameters to `t.group` because datetime format
-- strings in test parameters contains the symbol `/` that is not
-- allowed in testcases names. The source code below inserts
-- test functions into a test group with testnames where `/` is
-- replaced with `_`.
for supported_by, standard_cases in pairs(SUPPORTED_DATETIME_FORMATS) do
    for _, case in ipairs(standard_cases) do
        local f = case.fmt
        local testcase_name = 'test_supported_format_' .. f:gsub('/', '_')
        local fmtmsg = "Format '%s' supported by %s not parsed by %s"
        local invalmsg = 'invalid result: datetime:%s'

        if supported_by == 'RFC3339 AND ISO8601' then
            local buf = case.buf

            pg[testcase_name] = function()
                local iso8601_ok, iso8601_val = pcall(dt.parse, buf,
                                                      {format = 'iso8601'})
                local rfc3339_ok, rfc3339_val = pcall(dt.parse, buf,
                                                      {format = 'rfc3339'})
                t.assert(iso8601_ok, fmtmsg:format(f, supported_by, 'iso8601'))
                t.assert(rfc3339_ok, fmtmsg:format(f, supported_by, 'rfc3339'))
                t.assert_equals(iso8601_val, rfc3339_val, 'unequal results')
                if case.ts ~= nil then
                    t.assert_equals(iso8601_val.timestamp, case.ts,
                                    invalmsg:format(iso8601_val:format(f)))
                end
            end
        else
            local dtfmt = supported_by:gsub(' ONLY', ''):lower()
            pg[testcase_name] = function()
                local ok, val = pcall(dt.parse, case.buf, {format = dtfmt})
                t.assert(ok, fmtmsg:format(f, supported_by, dtfmt))
                if case.ts ~= nil then
                    t.assert_equals(val.timestamp, case.ts,
                                    invalmsg:format(val:format(case.fmt)))
                end
            end
        end
    end
end

for supported_by, standard_cases in pairs(UNSUPPORTED_DATETIME_FORMATS) do
    for _, case in ipairs(standard_cases) do
        local f = case.fmt
        local testcase_name = 'test_unsupported_format_' .. f:gsub('/', '_')
        local fmtmsg = "Unsupported by Tarantool format '%s' " ..
                       "supported by %s is parsed by %s"

        if supported_by == 'RFC3339 AND ISO8601' then
            local buf = case.buf
            pg[testcase_name] = function()
                local iso8601_ok, _ = pcall(dt.parse, buf, {format = 'iso8601'})
                local rfc3339_ok, _ = pcall(dt.parse, buf, {format = 'rfc3339'})
                t.assert(not iso8601_ok, fmtmsg:format(f, supported_by,
                                                       'iso8601'))
                t.assert(not rfc3339_ok, fmtmsg:format(f, supported_by,
                                                       'rfc3339'))
            end
        else
            local dtfmt = supported_by:gsub(' ONLY', ''):lower()
            pg[testcase_name] = function()
                local ok, _ = pcall(dt.parse, case.buf, {format = dtfmt})
                t.assert(not ok, fmtmsg:format(f, supported_by, dtfmt))
            end
        end
    end
end

-- {{{ new() and set() invalid args test.

-- For set() we must fill all components to check possible corruption.
local SET_BASE_DATE_TIME_UNITS = dt.new({
    year = 2021, month = 2, day = 3,
    hour = 12, min = 34, sec = 56,
    nsec = 123456789, tz = 'Europe/Moscow'}):totable()
-- Workaround for gh-12619.
SET_BASE_DATE_TIME_UNITS.timestamp = nil

-- Timestamp fixing for gh-10363 workaround.
local function to_local_time(timestamp)
    checks('number')
    return timestamp + SET_BASE_DATE_TIME_UNITS.tzoffset * 60
end
local function to_utc_time(timestamp)
    checks('number')
    return timestamp - SET_BASE_DATE_TIME_UNITS.tzoffset * 60
end

local INVALID_NEW_AND_SET_TIME_UNITS_ERRORS = {
    only_one_of = 'only one of nsec, usec or msecs may be defined'..
        ' simultaneously',
    only_integer_ts = 'only integer values allowed in timestamp'..
        ' if nsec, usec, or msecs provided',
    timestamp_and_ymd = 'timestamp is not allowed if year/month/day provided',
    timestamp_and_hms = 'timestamp is not allowed if hour/min/sec provided',

    only_integer_msg = function(set_arg)
        local key, _ = get_single_key_val(set_arg, true)
        return key .. ': integer value expected, but received number'
    end,

    numeric_exp = function(set_arg)
        local _, val = get_single_key_val(set_arg, true)
        return 'numeric value expected, but received '..type(val)
    end,

    expected_type = function(set_arg, typename, msg)
        local _, val = get_single_key_val(set_arg, false)
        return ("%s: expected %s, but received %s"):format(msg, typename, type(val))
    end,

    expected_type2 = function(set_arg, what_expected)
        local key, val = get_single_key_val(set_arg, true)
        return ("%s: %s expected, but received %s"):format(key, what_expected, val)
    end,

    expected_type3 = function(set_arg, what_expected)
        local key, val = get_single_key_val(set_arg, true)
        return ("bad %s ('%s' expected, got '%s')"):format(key, what_expected, type(val))
    end,

    range_check_error_string = function(set_arg, range)
        local key, val = get_single_key_val(set_arg, true)
        return ('value %s of %s is out of allowed range [%s, %s]'):
              format(val, key, range[1], range[2])
    end,

    -- Using %s conversion for large integer values produce
    -- scientific-format strings. This fn is for the case,
    -- when precise integer representation is needed.
    range_check_error_digit = function(set_arg, range)
        local key, val = get_single_key_val(set_arg, true)
        return ('value %d of %s is out of allowed range [%d, %d]'):
              format(val, key, range[1], range[2])
    end,

    range_check_error_set_timestamp = function(set_arg, range)
        checks({timestamp = 'number'}, {[1] = 'number', [2] = 'number'})
        local key, val = get_single_key_val(set_arg, true)
        -- Workaround for gh-10363.
        val = to_utc_time(val)
        return ('value %d of %s is out of allowed range [%d, %d]'):
              format(val, key, range[1], range[2])
    end,

    range_check_3_error = function(set_arg, range)
        local key, val = get_single_key_val(set_arg, true)
        return ('value %d of %s is out of allowed range [%d, %d..%d]'):
            format(val, key, range[1], range[2], range[3])
    end,

    invalid_days_in_mon = function(set_arg)
        local msg = 'misconfig: expected table {day = d, month = M, year = y}'
        local d, M, y = set_arg.day, set_arg.month, set_arg.year
        t.fail_if(d == nil, msg)
        t.fail_if(M == nil, msg)
        t.fail_if(y == nil, msg)
        return ('invalid number of days %d in month %d for %d'):format(d, M, y)
    end,

    invalid_date = function(set_arg)
        local msg = 'misconfig: expected table {day = d, month = M, year = y}'
        local d, M, y = set_arg.day, set_arg.month, set_arg.year
        t.fail_if(d == nil, msg)
        t.fail_if(M == nil, msg)
        t.fail_if(y == nil, msg)
        return ('date %d-%02d-%02d is invalid'):format(y, M, d)
    end,

    couldnt_parse_tz = function(set_arg, msg_tail)
        checks('?', '?string')
        msg_tail = msg_tail or ''
        local _, val = get_single_key_val(set_arg, true)
        return ('could not parse \'%s\''):format(val)..msg_tail
    end,

    invalid_tzoffset_fmt_error = function(set_arg)
        local _, val = get_single_key_val(set_arg, true)
        return ('invalid time-zone format %s'):format(val)
    end,
}

local INVALID_NEW_AND_SET_TIME_UNITS = {
    -- Fractional unit mix tests.
    {
        set = {nsec = 123456, usec = 123},
        err_key = 'only_one_of',
    },
    {
        set = {nsec = 123456, msec = 123},
        err_key = 'only_one_of',
    },
    {
        set = {usec = 123, msec = 123},
        err_key = 'only_one_of',
    },
    {
        set = {nsec = 123456, usec = 123, msec = 123},
        err_key = 'only_one_of',
    },
    -- Timestamp plus units mixed tests.
    {
        set = {timestamp = 12345.125, msec = 123},
        err_key = 'only_integer_ts',
    },
    {
        set = {timestamp = 12345.125, usec = 123},
        err_key = 'only_integer_ts',
    },
    {
        set = {timestamp = 12345.125, nsec = 123},
        err_key = 'only_integer_ts',
    },
    {
        set = {timestamp = 1630359071.125, year = 2021},
        err_key = 'timestamp_and_ymd',
    },
    {
        set = {timestamp = 1630359071.125, month = 9},
        err_key = 'timestamp_and_ymd',
    },
    {
        set = {timestamp = 1630359071.125, day = 29},
        err_key = 'timestamp_and_ymd',
    },
    {
        set = {timestamp = 1630359071.125, hour = 20},
        err_key = 'timestamp_and_hms',
    },
    {
        set = {timestamp = 1630359071.125, min = 10},
        err_key = 'timestamp_and_hms',
    },
    {
        set = {timestamp = 1630359071.125, sec = 29},
        err_key = 'timestamp_and_hms',
    },
    -- Type tests.
    {
        set_multiple = {'2001-01-01', 20010101},
        err_fn = 'expected_type',
        _new = {err_fn_args = {'table', 'datetime.new()'}},
        _set = {err_fn_args = {'table', 'datetime.set()'}},
    },
    {
        set_multiple = {{year = {}}, {year = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {year = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{month = {}}, {month = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {month = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{day = {}}, {day = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {day = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{hour = {}}, {hour = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {hour = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{min = {}}, {min = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {min = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{sec = {}}, {sec = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {sec = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{msec = {}}, {msec = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {msec = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{usec = {}}, {usec = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {usec = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{nsec = {}}, {nsec = dt.new()}},
        err_fn = 'numeric_exp',
    },
    {
        set = {nsec = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set_multiple = {{timestamp = '3600.1'}, {timestamp = true}},
        err_fn = 'expected_type3',
        err_fn_args = {'number'},
        _set = {compat = {datetime_setfn_timestamp_type_check = 'new'}},
    },
    {
        compat = {datetime_setfn_timestamp_type_check = 'old'},
        set = {timestamp = true},
        err_msg = 'bad argument #1 to \'math_modf\' '..
            '(number expected, got boolean)',
        _new = {skip = 'only set() - old behaviour'},
    },
    {
        set_multiple = {{tzoffset = {}}, {tzoffset = dt.new()}},
        err_fn = 'expected_type2',
        err_fn_args = {'string or number'},
    },
    {
        set = {tzoffset = 1.1},
        err_fn = 'only_integer_msg',
    },
    {
        set = {tz = 400},
        err_fn = 'expected_type',
        err_fn_args = {'string', 'parse_tzname()'},
    },
    -- Tzoffset parse tests.
    {
        set_multiple = {
            {tzoffset = '+03:00 what?'},
            {tzoffset = '-0000 '},
            {tzoffset = '+0000 '},
            {tzoffset = 'bogus'},
            {tzoffset = '0100'},
            {tzoffset = '+-0100'},
            {tzoffset = '+25:00'},
            {tzoffset = '+9900'},
            {tzoffset = '-99:00'},
        },
        err_fn = 'invalid_tzoffset_fmt_error',
    },
    -- Timezone parse tests.
    {
        set = {tz = 'zzzYYYwww'},
        err_fn = 'couldnt_parse_tz',
    },
    -- See lib/tzcode/timezones.h for erroneous zones.
    {
        -- Zones with TZ_AMBIGUOUS flag.
        set = {tz = 'ECT'},
        err_fn = 'couldnt_parse_tz',
        err_fn_args = {' - ambiguous timezone'},
    },
    {
        -- Zones with TZ_NYI flag.
        set = {tz = '_'},
        err_fn = 'couldnt_parse_tz',
        err_fn_args = {' - nyi timezone'},
        skip = 'no zones with TZ_NYI flag',
    },
    -- Single unit range tests.
    {
        set_range = {'year', YEAR_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {YEAR_RANGE},
    },
    {
        set_range = {'month', MONTH_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {MONTH_RANGE},
    },
    {
        set_range = {'day', DAY_RANGE},
        err_fn = 'range_check_3_error',
        err_fn_args = {{-1, 1, 31}},
    },
    {
        set_range = {'hour', HOUR_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {HOUR_RANGE},
    },
    {
        set_range = {'min', MINUTE_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {MINUTE_RANGE},
    },
    {
        set_range = {'sec', SEC_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {SEC_RANGE},
    },
    -- new() test: we create new datetime object with tzoffset = 0,
    -- therefore workaround for gh-10363 is not needed.
    {
        set_range = {'timestamp', TIMESTAMP_RANGE},
        err_fn = 'range_check_error_digit',
        err_fn_args = {TIMESTAMP_RANGE},
        _set = {skip = 'need workaround for gh-10363'},
    },
    -- set() test: we use source object with tzoffset ~= 0 and
    -- workaround for gh-10363 is needed.
    {
        set_multiple = {
            {timestamp = to_local_time(TIMESTAMP_RANGE[1] - 1)},
            {timestamp = to_local_time(TIMESTAMP_RANGE[1] - 50)},
            {timestamp = to_local_time(TIMESTAMP_RANGE[2] + 1)},
            {timestamp = to_local_time(TIMESTAMP_RANGE[2] + 50)},
        },
        err_fn = 'range_check_error_set_timestamp',
        err_fn_args = {TIMESTAMP_RANGE},
        _new = {skip = 'only set'},
    },
    {
        set_range = {'msec', MSEC_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {MSEC_RANGE},
    },
    {
        set_range = {'usec', USEC_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {USEC_RANGE},
    },
    {
        set_range = {'nsec', NSEC_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {NSEC_RANGE},
    },
    {
        set_range = {'tzoffset', TZOFFSET_RANGE},
        err_fn = 'range_check_error_string',
        err_fn_args = {TZOFFSET_RANGE},
    },
    -- Date range tests.
    {
        set = {year = 2021, month = 6, day = 31},
        err_fn = 'invalid_days_in_mon',
    },
    {
        set = {year = MIN_DATE_YEAR, month = MIN_DATE_MONTH - 1, day = 1},
        err_fn = 'invalid_date',
    },
    {
        set = {
            year = MIN_DATE_YEAR,
            month = MIN_DATE_MONTH,
            day = MIN_DATE_DAY - 1
        },
        err_fn = 'invalid_date',
    },
    {
        set = {
            year = MAX_DATE_YEAR,
            month = MAX_DATE_MONTH,
            day = MAX_DATE_DAY + 1
        },
        err_fn = 'invalid_date',
    },
    {
        set = {year = MAX_DATE_YEAR, month = MAX_DATE_MONTH + 1, day = 1},
        err_fn = 'invalid_date',
    },
}

-- The test covers new() and set() errors due to invalid arguments passed.
local function test_invalid_new_and_set_time_units(cg, new_test)
    local par = cg.params
    local function check_par(_)
        checks({
            -- The reason to skip test.
            skip = '?string',
            -- Table with compat option required for test.
            compat = '?table',
            -- Test set for single test.
            set = '?',
            -- Run several single tests with same error.
            set_multiple = '?table',
            -- Test set for range test.
            -- It runs several single tests.
            set_range = '?table',
            -- Raw error message.
            err_msg = '?string',
            -- Key of raw error message in a table
            -- INVALID_NEW_AND_SET_TIME_UNITS_ERRORS (T).
            err_key = '?string',
            -- Key of function, generating error message in T.
            -- It's first arg is `set`.
            err_fn = '?string',
            -- Addidional args for `err_fn`.
            err_fn_args = '?table',
            -- Overrides for new() test.
            _new = '?table',
            -- Overrides for set() test.
            _set = '?table',
        })
    end
    check_par(par)

    -- Function related params adjustment.
    local p
    if new_test and par._new ~= nil then
        p = fun.chain(par, par._new):tomap()
    elseif not new_test and par._set ~= nil then
        p = fun.chain(par, par._set):tomap()
    else
        p = par
    end
    t.skip_if(p.skip ~= nil, p.skip)

    local function new_tester(set, expected_error)
        checks('?', 'string')
        t.assert_error_msg_contains(expected_error, dt.new, set)
    end
    local function set_tester(set, expected_error)
        checks('?', 'string')
        local d = dt.new(SET_BASE_DATE_TIME_UNITS)
        local before = d:totable()
        t.assert_error_msg_contains(expected_error, d.set, d, set)
        local after = d:totable()
        t.assert_equals(after, before, 'd was changed')
    end
    local tester = new_test and new_tester or set_tester

    local function single_test(set)
        -- Prepare test error message.
        local error
        if p.err_key ~= nil then
            error = INVALID_NEW_AND_SET_TIME_UNITS_ERRORS[p.err_key]
        elseif p.err_fn ~= nil then
            local fn = INVALID_NEW_AND_SET_TIME_UNITS_ERRORS[p.err_fn]
            t.fail_if(type(fn) ~= 'function', 'misconfig')
            local err_fn_args = p.err_fn_args or {}
            error = fn(set, unpack(err_fn_args))
        elseif p.err_msg ~= nil then
            error = p.err_msg
        else
            t.fail('misconfig')
        end
        -- Check for required error.
        tester(set, error)
    end

    local function multiple_test(set_multiple)
        t.fail_if(type(set_multiple) ~= 'table', 'misconfig')
        for _, set in pairs(set_multiple) do
            single_test(set)
        end
    end

    local function range_test(key, range)
        t.fail_if(type(key) ~= 'string', 'misconfig')
        t.fail_if(type(range) ~= 'table', 'misconfig')
        local min, max = range[1], range[2]
        t.fail_if(min == nil, 'misconfig')
        t.fail_if(max == nil, 'misconfig')
        single_test({[key] = min - 1})
        single_test({[key] = min - 50})
        single_test({[key] = max + 1})
        single_test({[key] = max + 50})
    end

    -- Switch compat settings if required.
    if p.compat ~= nil then
        local k, v = get_single_key_val(p.compat, true)
        compat[k] = v
    end
    -- Run test.
    if p.set ~= nil then
        single_test(p.set)
    elseif p.set_multiple ~= nil then
        multiple_test(p.set_multiple)
    elseif p.set_range ~= nil then
        range_test(p.set_range[1], p.set_range[2])
    else
        t.fail('misconfig')
    end
    -- Restore compat settings to default.
    if p.compat ~= nil then
        local k, _ = get_single_key_val(p.compat, true)
        compat[k] = 'default'
    end
end

local g_fail_time_units = t.group('fail_time_units',
    INVALID_NEW_AND_SET_TIME_UNITS)

g_fail_time_units.test_new = function(cg)
    test_invalid_new_and_set_time_units(cg, true)
end

g_fail_time_units.test_set = function(cg)
    test_invalid_new_and_set_time_units(cg, false)
end

-- }}} new() and set() invalid args test.

local INVALID_ISO_STRINGS = {
    {
        -- '8400329 10-22' case was found by fuzzer.
        buf = '8400329 10'..tzoffset_fmt((MIN_TZOFFSET_H - 1) * 60, 'H'),
        comment = 'out of range tzoffset',
    },
    {
        buf = '8400329 10'..tzoffset_fmt((MAX_TZOFFSET_H + 1) * 60, 'H'),
        comment = 'out of range tzoffset',
    },
    {
        buf = '8400329 10'..tzoffset_fmt(MIN_TZOFFSET - 1, ''),
        comment = 'out of range tzoffset',
    },
    {
        buf = '8400329 10'..tzoffset_fmt(MAX_TZOFFSET + 1, ''),
        comment = 'out of range tzoffset',
    },
    {
        buf = '8400329 10'..tzoffset_fmt(MIN_TZOFFSET - 1, 'X'),
        comment = 'out of range tzoffset',
    },
    {
        buf = '8400329 10'..tzoffset_fmt(MAX_TZOFFSET + 1, 'X'),
        comment = 'out of range tzoffset',
    },
}

local pg2 = t.group('parse_iso_fail', INVALID_ISO_STRINGS)

pg2.test_parse_iso_fail = function(cg)
    local p = cg.params
    t.assert_error_msg_contains('could not parse', dt.parse, p.buf)
end
