local ffi = require('ffi')
local buffer = require('buffer')
---@type table tz # Maps timezone names and abbreviations to their index
---    and vice-versa
local tz = require('timezones')
local tweaks = require('internal.tweaks')

--[[
    `c-dt` library functions handles properly both positive and negative `dt`
    values, where `dt` is a number of dates since Rata Die date (0001-01-01).

    `c-dt` uses 32-bit integer number to store `dt` values, so range of
    supported dates is limited to dates from -5879610-06-22 (INT32_MIN) to
    +5879611-07-11 (INT32_MAX).

    For better compactness of our typical data in MessagePack stream we shift
    root of our time to the Unix Epoch date (1970-01-01), thus our 0 is
    actually dt = 719163.

    So here is a simple formula how convert our epoch-based seconds to dt values
        dt = (secs / 86400) + 719163
    Where 719163 is an offset of Unix Epoch (1970-01-01) since Rata Die
    (0001-01-01) in dates.
]]

ffi.cdef[[
typedef long time_t;

/* dt_core.h definitions */
typedef int dt_t;

typedef enum {
    DT_MON       = 1,
    DT_MONDAY    = 1,
    DT_TUE       = 2,
    DT_TUESDAY   = 2,
    DT_WED       = 3,
    DT_WEDNESDAY = 3,
    DT_THU       = 4,
    DT_THURSDAY  = 4,
    DT_FRI       = 5,
    DT_FRIDAY    = 5,
    DT_SAT       = 6,
    DT_SATURDAY  = 6,
    DT_SUN       = 7,
    DT_SUNDAY    = 7,
} dt_dow_t;

dt_t   tnt_dt_from_rdn     (int n);
bool   tnt_dt_from_ymd_checked(int y, int m, int d, dt_t *val);
void   tnt_dt_to_ymd       (dt_t dt, int *y, int *m, int *d);

dt_dow_t tnt_dt_dow        (dt_t dt);

/* dt_util.h */
int     tnt_dt_days_in_month   (int y, int m);

/* dt_accessor.h */
int     tnt_dt_year         (dt_t dt);
int     tnt_dt_month        (dt_t dt);
int     tnt_dt_doy          (dt_t dt);
int     tnt_dt_dom          (dt_t dt);

dt_t   tnt_dt_add_months   (dt_t dt, int delta, dt_adjust_t adjust);

/* dt_parse_iso.h definitions */
size_t tnt_dt_parse_iso_date(const char *str, size_t len, dt_t *dt);
size_t tnt_dt_parse_iso_zone_lenient(const char *str, size_t len, int *offset);

/* Tarantool datetime functions - datetime.c */
size_t tnt_datetime_to_string(const struct datetime * date, char *buf,
                              ssize_t len);
size_t tnt_datetime_strftime(const struct datetime *date, char *buf,
                             uint32_t len, const char *fmt);
ssize_t tnt_datetime_parse_full(struct datetime *date, const char *str,
                                size_t len);
ssize_t tnt_datetime_parse_tz(const char *str, size_t len, time_t base,
                              int16_t *tzoffset, int16_t *tzindex);
size_t tnt_datetime_strptime(struct datetime *date, const char *buf,
                             const char *fmt);
void   tnt_datetime_now(struct datetime *now);
bool   tnt_datetime_totable(const struct datetime *date,
                            struct interval *out);
bool   tnt_datetime_isdst(const struct datetime *date);

/* Tarantool interval support functions */
size_t tnt_interval_to_string(const struct interval *, char *, ssize_t);
int    tnt_datetime_increment_by(struct datetime *self, int direction,
                                 const struct interval *ival);
int    tnt_datetime_datetime_sub(struct interval *res,
                                 const struct datetime *lhs,
                                 const struct datetime *rhs);

int    tnt_interval_interval_sub(struct interval *lhs,
                                 const struct interval *rhs);
int    tnt_interval_interval_add(struct interval *lhs,
                                 const struct interval *rhs);

/* Tarantool timezone support */
enum {
    TZ_UTC = 0x01,
    TZ_RFC = 0x02,
    TZ_MILITARY = 0x04,
    TZ_AMBIGUOUS = 0x08,
    TZ_NYI = 0x10,
};

]]

---@alias datetime.units {
---    nsec?: integer,
---    usec?: integer,
---    msec?: integer,
---    sec?: integer,
---    min?: integer,
---    hour?: integer,
---    day?: integer,
---    month?: integer,
---    year?: integer,
---    timestamp?: number,
---    tzoffset?: integer,
---    tz?: string,
---}

---@alias datetime.interval.adjust 'none' | 'last' | 'excess'

---@alias datetime.interval.units {
---    nsec?: integer,
---    sec?: integer,
---    min?: integer,
---    hour?: integer,
---    day?: integer,
---    week?: integer,
---    month?: integer,
---    year?: integer,
---    adjust?: datetime.interval.adjust,
---}

---@class datetime: ffi.cdata*
---@field nsec integer (Default: 0) (usec, msec) Fractional part of the last
---    second. You can specify either nanoseconds (nsec), or microseconds
---    (usec), or milliseconds (msec). Specifying two of these units
---    simultaneously or all three ones lead to an error.
---@field sec integer (Default: 0) Seconds. Value range: 0 - 60.
---@field min integer (Default: 0) Minutes. Value range: 0 - 59.
---@field hour integer (Default: 0) Hours. Value range: 0 - 23.
---@field day integer (Default: 1) Day number. Value range: 1 - 31. The
---    special value -1 generates the last day of a particular month.
---@field month integer (Default: 1) Month number. Value range: 1 - 12.
---@field year integer (Default: 1970) Year.
---@field timestamp number (Default: 0) Timestamp, in seconds. Similar to the
---    Unix timestamp, but can have a fractional part which is converted in
---    nanoseconds in the resulting datetime object. If the fractional part
---    for the last second is set via the nsec, usec, or msec units, the
---    timestamp value should be integer otherwise an error occurs. Timestamp
---    is not allowed if you already set time and/or date via specific units,
---    namely, sec, min, hour, day, month, and year.
---@field tzoffset integer (Default: 0) Time zone offset from UTC, in
---    minutes. If both tzoffset and tz are specified, tz has the preference
---    and the tzoffset value is ignored.
---@field tz string Time zone name according to the tz database.
---@field wday integer Days since the beginning of the week.
---@field yday integer Days since the beginning of the year.
---@field isdst boolean Is the DST (Daylight saving time) applicable for the
---    date.
---@field usec integer Read-only. Nanoseconds / 1e3.
---@field msec integer Read-only. Nanoseconds / 1e6.
---@field format fun(self: datetime, fmt?: string): string
---@field totable fun(self: datetime): datetime.totable
---@field set fun(self: datetime, units: datetime.units): datetime
---@field add fun(self: datetime,
---    input: datetime.interval.units | datetime.interval): datetime
---@field sub fun(self: datetime,
---    input: datetime.interval.units | datetime.interval): datetime
---@operator add(datetime.interval): datetime
---@operator sub(datetime.interval): datetime

---@class datetime.totable
---@field nsec integer Nanoseconds.
---@field sec integer Seconds.
---@field min integer Minutes.
---@field hour integer Hours.
---@field day integer Day number.
---@field month integer Month number.
---@field year integer Year.
---@field wday integer Days since the beginning of the week.
---@field yday integer Days since the beginning of the year.
---@field timestamp number Timestamp, in seconds.
---@field isdst boolean Whether DST is applicable for the date.
---@field tzoffset integer Time zone offset from UTC, in minutes.
---@field tz string Time zone name or abbreviation.

---@class datetime.interval: ffi.cdata*
---@field nsec integer (Default: 0) (usec, msec) Fractional part of the last
---    second. You can specify either nanoseconds (nsec), or microseconds
---    (usec), or milliseconds (msec). Specifying two of these units
---    simultaneously or all three ones lead to an error.
---@field sec integer (Default: 0) Seconds.
---@field min integer (Default: 0) Minutes.
---@field hour integer (Default: 0) Hours.
---@field day integer (Default: 0) Day number.
---@field week integer (Default: 0) Week number.
---@field month integer (Default: 0) Month number.
---@field year integer (Default: 0) Year.
---@field adjust datetime.interval.adjust (Default: 'none') Defines how to
---    round days in a month after an arithmetic operation.
---@field usec integer Read-only. Nanoseconds / 1e3.
---@field msec integer Read-only. Nanoseconds / 1e6.
---@field totable fun(self: datetime.interval): datetime.interval.totable
---@operator add(datetime.interval): datetime.interval
---@operator sub(datetime.interval): datetime.interval

---@class datetime.interval.totable
---@field nsec integer
---@field sec integer
---@field min integer
---@field hour integer
---@field day integer
---@field week integer
---@field month integer
---@field year integer
---@field adjust datetime.interval.adjust|nil

local builtin = ffi.C
local math_modf = math.modf
local math_floor = math.floor

-- Unix, January 1, 1970, Thursday
local DAYS_EPOCH_OFFSET = 719163
local SECS_PER_DAY      = 86400
local SECS_EPOCH_OFFSET = DAYS_EPOCH_OFFSET * SECS_PER_DAY
local TOSTRING_BUFSIZE  = 64
local IVAL_TOSTRING_BUFSIZE = 96
local STRFTIME_BUFSIZE  = 128

-- At the moment the range of known timezones is UTC-12:00..UTC+14:00. See [1].
-- 1. https://en.wikipedia.org/wiki/List_of_UTC_time_offsets.
local TZOFFSET_MIN = -12 * 60
local TZOFFSET_MAX = 14 * 60

-- minimum supported date - -5879610-06-22
local MIN_DATE_YEAR = -5879610
local MIN_DATE_MONTH = 6
local MIN_DATE_DAY = 22
-- maximum supported date - 5879611-07-11
local MAX_DATE_YEAR = 5879611
local MAX_DATE_MONTH = 7
local MAX_DATE_DAY = 11
-- In the Julian calendar, the average year length is
-- 365 1/4 days = 365.25 days. This gives an error of
-- about 1 day in 128 years.
local AVERAGE_DAYS_YEAR = 365.25
local AVERAGE_WEEK_YEAR = AVERAGE_DAYS_YEAR / 7
local INT_MAX = 2147483647
local INT_MIN = -2147483648
-- -5879610-06-22
local MIN_DATE_TEXT = ('%d-%02d-%02d'):format(MIN_DATE_YEAR, MIN_DATE_MONTH,
                                              MIN_DATE_DAY)
-- 5879611-07-11
local MAX_DATE_TEXT = ('%d-%02d-%02d'):format(MAX_DATE_YEAR, MAX_DATE_MONTH,
                                              MAX_DATE_DAY)
local MIN_DT_DAY_VALUE = INT_MIN
local MAX_DT_DAY_VALUE = INT_MAX
local MIN_EPOCH_SECS_VALUE = MIN_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_OFFSET
local MAX_EPOCH_SECS_VALUE = MAX_DT_DAY_VALUE * SECS_PER_DAY - SECS_EPOCH_OFFSET
local MAX_YEAR_RANGE = MAX_DATE_YEAR - MIN_DATE_YEAR
local MAX_MONTH_RANGE = MAX_YEAR_RANGE * 12
local MAX_WEEK_RANGE = MAX_YEAR_RANGE * AVERAGE_WEEK_YEAR
local MAX_DAY_RANGE = MAX_YEAR_RANGE * AVERAGE_DAYS_YEAR
local MAX_HOUR_RANGE = MAX_DAY_RANGE * 24
local MAX_MIN_RANGE = MAX_HOUR_RANGE * 60
local MAX_SEC_RANGE = MAX_DAY_RANGE * SECS_PER_DAY
local MAX_NSEC_RANGE = INT_MAX
local MAX_USEC_RANGE = math_floor(MAX_NSEC_RANGE / 1e3)
local MAX_MSEC_RANGE = math_floor(MAX_NSEC_RANGE / 1e6)
local DEF_DT_ADJUST = builtin.DT_LIMIT

local date_tostr_stash =
    buffer.ffi_stash_new(string.format('char[%s]', TOSTRING_BUFSIZE))
local date_tostr_stash_take = date_tostr_stash.take
local date_tostr_stash_put = date_tostr_stash.put

local ival_tostr_stash =
    buffer.ffi_stash_new(string.format('char[%s]', IVAL_TOSTRING_BUFSIZE))
local ival_tostr_stash_take = ival_tostr_stash.take
local ival_tostr_stash_put = ival_tostr_stash.put

local date_strf_stash =
    buffer.ffi_stash_new(string.format('char[%s]', STRFTIME_BUFSIZE))
local date_strf_stash_take = date_strf_stash.take
local date_strf_stash_put = date_strf_stash.put

local date_dt_stash = buffer.ffi_stash_new('dt_t[1]')
local date_dt_stash_take = date_dt_stash.take
local date_dt_stash_put = date_dt_stash.put

local date_int16_stash = buffer.ffi_stash_new('int16_t[1]')
local date_int16_stash_take = date_int16_stash.take
local date_int16_stash_put = date_int16_stash.put

local datetime_t = ffi.typeof('struct datetime')
local interval_t = ffi.typeof('struct interval')

---Check whether the specified value is an `interval` object.
---
---*Since 3.2.0*
---
---@param o any the value to check.
---@return boolean result true if the specified value is an `interval` object,
---    otherwise false.
local function is_interval(o)
    return ffi.istype(interval_t, o)
end

---Check whether the specified value is a `datetime` object.
---
---@param o any the value to check.
---@return boolean result true if the specified value is a `datetime` object,
---    otherwise false.
local function is_datetime(o)
    return ffi.istype(datetime_t, o)
end

local function is_table(o)
    return type(o) == 'table'
end

local function check_date(o, message)
    if not is_datetime(o) then
        return error(("%s: expected datetime, but received %s"):
                     format(message, type(o)), 2)
    end
end

local function check_date_interval(o, message)
    if not is_datetime(o) and not is_interval(o) then
        return error(("%s: expected datetime or interval, but received %s"):
                     format(message, type(o)), 2)
    end
end

local function check_interval(o, message)
    if not is_interval(o) then
        return error(("%s: expected interval, but received %s"):
                     format(message, type(o)), 2)
    end
end

local function check_interval_table(o, message)
    if not is_table(o) and not is_interval(o) then
        return error(("%s: expected interval or table, but received %s"):
                     format(message, type(o)), 2)
    end
end

local function check_date_interval_table(o, message)
    if not is_table(o) and not is_datetime(o) and not is_interval(o) then
        return error(("%s: expected datetime, interval or table, but received %s"):
                     format(message, type(o)), 2)
    end
end

local function check_table(o, message)
    if not is_table(o) then
        return error(("%s: expected table, but received %s"):
                     format(message, type(o)), 2)
    end
end

local function check_str(s, message)
    if type(s) ~= 'string' then
        return error(("%s: expected string, but received %s"):
                     format(message, type(s)), 2)
    end
end

local function check_integer(v, message, error_level_up)
    error_level_up = error_level_up or 0
    if v == nil then
        return
    end
    if type(v) ~= 'number' or v % 1 ~= 0 then
        error(('%s: integer value expected, but received %s'):
              format(message, type(v)), 4 + error_level_up)
    end
end

local function check_str_or_nil(s, message)
    if s ~= nil and type(s) ~= 'string' then
        return error(("%s: expected string, but received %s"):
                     format(message, type(s)), 2)
    end
end

-- range may be of a form of pair {from, to} or
-- tuple {from, to, -1 in extra}
-- -1 is a special value (so far) used for days only
local function check_range(v, from, to, txt, extra, error_level_up)
    error_level_up = error_level_up or 0
    if type(v) ~= 'number' then
        error(('numeric value expected, but received %s'):
              format(type(v)), 3 + error_level_up)
    end
    if extra == v or (v >= from and v <= to) then
        return
    end
    if extra == nil then
        error(('value %d of %s is out of allowed range [%d, %d]'):
              format(v, txt, from, to), 3 + error_level_up)
    else
        error(('value %d of %s is out of allowed range [%d, %d..%d]'):
              format(v, txt, extra, from, to), 3 + error_level_up)
    end
end

local function dt_from_ymd(y, M, d)
    if d < 0 then
        d = builtin.tnt_dt_days_in_month(y, M)
    elseif d > 28 then
        local day_in_month = builtin.tnt_dt_days_in_month(y, M)
        if d > day_in_month then
            error(('invalid number of days %d in month %d for %d'):
                  format(d, M, y), 4)
        end
    end

    local pdt = date_dt_stash_take()
    local is_valid = builtin.tnt_dt_from_ymd_checked(y, M, d, pdt)
    if not is_valid then
        date_dt_stash_put(pdt)
        error(('date %4d-%02d-%02d is invalid'):format(y, M, d))
    end
    local dt = pdt[0]
    date_dt_stash_put(pdt)
    return dt
end

-- check v value against maximum/minimum possible values
-- if there is nil then simply return default 0
local function checked_max_value(v, max, txt, def)
    if v == nil then
        return def
    end
    if type(v) ~= 'number' then
        error(('numeric value expected, but received %s'):
              format(type(v)), 2)
    end
    if v >= -max and v <= max then
        return v
    end
    error(('value %s of %s is out of allowed range [%s, %s]'):
            format(v, txt, -max, max), 4)
end

local function bool2int(b)
    return b and 1 or 0
end

-- Forward declaration.
local datetime_set

local adjust_xlat = {
    none = builtin.DT_LIMIT,
    last = builtin.DT_SNAP,
    excess = builtin.DT_EXCESS,
}

local function interval_init(year, month, week, day, hour, min, sec, nsec,
                             adjust)
    return ffi.new(interval_t, sec, min, hour, day, week, month, year, nsec,
                   adjust)
end

local function interval_new_copy(obj)
    return interval_init(obj.year, obj.month, obj.week, obj.day, obj.hour,
                         obj.min, obj.sec, obj.nsec, obj.adjust)
end

local function interval_decode_args(obj)
    if is_interval(obj) then
        return obj
    end
    local year = checked_max_value(obj.year, MAX_YEAR_RANGE, 'year', 0)
    check_integer(year, 'year')
    local month = checked_max_value(obj.month, MAX_MONTH_RANGE, 'month', 0)
    check_integer(month, 'month')
    local adjust = adjust_xlat[obj.adjust] or DEF_DT_ADJUST

    local weeks = checked_max_value(obj.week, MAX_WEEK_RANGE, 'week', 0)
    local days = checked_max_value(obj.day, MAX_DAY_RANGE, 'day', 0)
    check_integer(days, 'day')
    local hours = checked_max_value(obj.hour, MAX_HOUR_RANGE, 'hour', 0)
    check_integer(hours, 'hour')
    local minutes = checked_max_value(obj.min, MAX_MIN_RANGE, 'min', 0)
    check_integer(minutes, 'min')
    local secs = checked_max_value(obj.sec, MAX_SEC_RANGE, 'sec', 0)
    check_integer(secs, 'sec')

    local nsec = checked_max_value(obj.nsec, MAX_NSEC_RANGE, 'nsec')
    local usec = checked_max_value(obj.usec, MAX_USEC_RANGE, 'usec')
    local msec = checked_max_value(obj.msec, MAX_MSEC_RANGE, 'msec')
    local count_usec = bool2int(nsec ~= nil) + bool2int(usec ~= nil) +
                       bool2int(msec ~= nil)
    if count_usec > 1 then
        error('only one of nsec, usec or msecs may be defined '..
                'simultaneously', 3)
    end
    nsec = (msec or 0) * 1e6 + (usec or 0) * 1e3 + (nsec or 0)

    return interval_init(year, month, weeks, days, hours, minutes, secs, nsec,
                         adjust)
end

---Create an object of the `interval` type from a table of time units.
---
---**Default values:**
---
---* `nsec: 0`
---* `sec: 0`
---* `min: 0`
---* `hour: 0`
---* `day: 0`
---* `week: 0`
---* `month: 0`
---* `year: 0`
---* `adjust: 'none'`
---
---@param obj? datetime.interval.units table with time units and parameters.
---@return datetime.interval interval_obj the created interval object.
local function interval_new(obj)
    if obj == nil then
        return interval_init(0, 0, 0, 0, 0, 0, 0, 0, DEF_DT_ADJUST)
    end
    check_table(obj, 'interval.new()')
    return interval_decode_args(obj)
end

-- convert from epoch related time to Rata Die related
local function local_rd(secs)
    return math_floor((secs + SECS_EPOCH_OFFSET) / SECS_PER_DAY)
end

-- convert UTC seconds to local seconds, adjusting by timezone
local function local_secs(obj)
    return obj.epoch + obj.tzoffset * 60
end

local function utc_secs(epoch, tzoffset)
    return epoch - tzoffset * 60
end

-- get epoch seconds, shift to the local timezone
-- adjust from 1970-related to 0000-related time
-- then return dt in those coordinates (number of days
-- since Rata Die date)
local function local_dt(obj)
    return builtin.tnt_dt_from_rdn(local_rd(local_secs(obj)))
end

local function datetime_cmp(lhs, rhs, is_raising)
    if not is_datetime(lhs) or not is_datetime(rhs) then
        if is_raising then
            error('incompatible types for datetime comparison', 3)
        else
            return nil
        end
    end
    local sdiff = lhs.epoch - rhs.epoch
    return sdiff ~= 0 and sdiff or (lhs.nsec - rhs.nsec)
end

local function datetime_eq(lhs, rhs)
    return datetime_cmp(lhs, rhs, false) == 0
end

local function datetime_lt(lhs, rhs)
    return datetime_cmp(lhs, rhs, true) < 0
end

local function datetime_le(lhs, rhs)
    return datetime_cmp(lhs, rhs, true) <= 0
end

--[[
    parse_tzoffset accepts time-zone strings in both basic
    and extended iso-8601 formats.

    Basic    Extended
    Z        N/A
    +hh      N/A
    -hh      N/A
    +hhmm    +hh:mm
    -hhmm    -hh:mm

    Returns timezone offset in minutes if string was accepted
    by parser, otherwise raise an error.
]]
local function parse_tzoffset(str, error_level_up)
    error_level_up = error_level_up or 0
    local offset = ffi.new('int[1]')
    local len = builtin.tnt_dt_parse_iso_zone_lenient(str, #str, offset)
    if len ~= #str then
        error(('invalid time-zone format %s'):format(str), 3 + error_level_up)
    end
    return offset[0]
end

local function epoch_from_dt(dt)
    return (dt - DAYS_EPOCH_OFFSET) * SECS_PER_DAY
end

-- Use Olson facilities to determine whether local time in obj is DST
local function datetime_isdst(obj)
    return builtin.tnt_datetime_isdst(obj)
end

-- Parse timezone name.
local function parse_tzname(base_epoch, tzname)
    check_str(tzname, 'parse_tzname()')
    local ptzindex = date_int16_stash_take()
    local ptzoffset = date_int16_stash_take()
    local len = builtin.tnt_datetime_parse_tz(tzname, #tzname, base_epoch,
                                              ptzoffset, ptzindex)
    if len > 0 then
        local tzoffset, tzindex = ptzoffset[0], ptzindex[0]
        date_int16_stash_put(ptzoffset)
        date_int16_stash_put(ptzindex)
        return tzoffset, tzindex
    end
    date_int16_stash_put(ptzoffset)
    date_int16_stash_put(ptzindex)
    if len == -builtin.TZ_NYI then
        error(("could not parse '%s' - nyi timezone"):format(tzname))
    elseif len == -builtin.TZ_AMBIGUOUS then
        error(("could not parse '%s' - ambiguous timezone"):format(tzname))
    else -- len <= 0
        error(("could not parse '%s'"):format(tzname))
    end
end

local function datetime_new_raw(epoch, nsec, tzoffset, tzindex)
    local dt_obj = ffi.new(datetime_t)
    dt_obj.epoch = epoch
    dt_obj.nsec = nsec
    dt_obj.tzoffset = tzoffset
    dt_obj.tzindex = tzindex
    return dt_obj
end

local function datetime_new_copy(obj)
    return datetime_new_raw(obj.epoch, obj.nsec, obj.tzoffset, obj.tzindex)
end

local function datetime_new_dt(dt, secs, nanosecs, offset, tzindex)
    secs = secs or 0
    nanosecs = nanosecs or 0
    offset = offset or 0
    tzindex = tzindex or 0
    return datetime_new_raw(epoch_from_dt(dt) + secs - offset * 60, nanosecs,
                            offset, tzindex)
end

local function extract_obj_ymd(obj)
    local y, M, d, ymd = obj.year, obj.month, obj.day, false
    if y ~= nil then
        check_range(y, MIN_DATE_YEAR, MAX_DATE_YEAR, 'year', nil, 1)
        check_integer(y, 'year', 1)
        ymd = true
    end
    if M ~= nil then
        check_range(M, 1, 12, 'month', nil, 1)
        check_integer(M, 'month', 1)
        ymd = true
    end
    if d ~= nil then
        check_range(d, 1, 31, 'day', -1, 1)
        check_integer(d, 'day', 1)
        ymd = true
    end
    return y, M, d, ymd
end

local function extract_obj_hms(obj)
    local h, m, s, hms = obj.hour, obj.min, obj.sec, false
    if h ~= nil then
        check_range(h, 0, 23, 'hour', nil, 1)
        check_integer(h, 'hour', 1)
        hms = true
    end
    if m ~= nil then
        check_range(m, 0, 59, 'min', nil, 1)
        check_integer(m, 'min', 1)
        hms = true
    end
    if s ~= nil then
        check_range(s, 0, 60, 'sec', nil, 1)
        check_integer(s, 'sec', 1)
        hms = true
    end
    return h, m, s, hms
end

-- Returns nsec if obj.nsec or obj.usec or obj.msec is defined.
-- Returns nil if not.
local function extract_obj_nsec(obj)
    local nsec, usec, msec = obj.nsec, obj.usec, obj.msec
    local count_usec = bool2int(nsec ~= nil) + bool2int(usec ~= nil) +
                       bool2int(msec ~= nil)
    if count_usec > 0 then
        if count_usec > 1 then
            error('only one of nsec, usec or msecs may be defined '..
                  'simultaneously', 3)
        end
        if usec ~= nil then
            check_range(usec, 0, 1e6, 'usec', nil, 1)
            check_integer(usec, 'usec', 1)
            nsec = usec * 1e3
        elseif msec ~= nil then
            check_range(msec, 0, 1e3, 'msec', nil, 1)
            check_integer(msec, 'msec', 1)
            nsec = msec * 1e6
        else
            check_range(nsec, 0, 1e9, 'nsec', nil, 1)
            check_integer(nsec, 'nsec', 1)
        end
    end
    return nsec
end

-- Returns UTC epoch if timestamp is defined.
-- Returns nsec as fraction part of timestamp if nsec isn't defined.
-- Returns has_nsec flag whether the returned nsec is defined.
-- Post: defined(epoch) => defined(nsec).
-- from_set is needed to prevent timestamp type check for set() to save
-- backward compatibility, see [1].
--
-- 1. https://github.com/tarantool/tarantool/issues/12411
local function extract_obj_epoch_and_update_nsec(obj, ymd, hms, nsec, from_set)
    local ts = obj.timestamp
    if ts == nil then
        return nil, nsec, nsec ~= nil
    end

    if ymd then
        error('timestamp is not allowed if year/month/day provided', 3)
    end
    if hms then
        error('timestamp is not allowed if hour/min/sec provided', 3)
    end
    if (not from_set or tweaks.datetime_setfn_timestamp_type_check) and
        type(ts) ~= 'number' then
        error(("bad timestamp ('number' expected, got '%s')"):format(type(ts)))
    end
    local epoch, fraction = math_modf(ts)
    -- In case of negative fraction part we should
    -- make it positive at the expense of the integer part.
    -- Code below expects that "nsec" value is always positive.
    if fraction < 0 then
        epoch = epoch - 1
        fraction = fraction + 1
    end
    -- if there are separate nsec, usec, or msec provided then
    -- timestamp should be integer
    if nsec == nil then
        nsec = fraction * 1e9
    elseif fraction ~= 0 then
        error('only integer values allowed in timestamp '..
                'if nsec, usec, or msecs provided', 3)
    end

    return epoch, nsec, nsec ~= nil
end

local function get_timezone(offset, msg, error_level_up)
    error_level_up = error_level_up or 0
    if type(offset) == 'number' then
        check_integer(offset, 'tzoffset', error_level_up)
        return offset
    elseif type(offset) == 'string' then
        return parse_tzoffset(offset, error_level_up)
    else
        error(('%s: string or number expected, but received %s'):
              format(msg, offset), 3 + error_level_up)
    end
end

-- base_epoch is needed for Olson timezone lookup.
local function extract_obj_tzoffset_tzindex(obj, base_epoch)
    local tzoffset, tzindex

    local obj_tzoffset = obj.tzoffset
    local tzname = obj.tz
    if tzname ~= nil then
         tzoffset, tzindex = parse_tzname(base_epoch, tzname)
    elseif obj_tzoffset ~= nil then
        tzindex = 0
        tzoffset = get_timezone(obj_tzoffset, 'tzoffset', 1)
        check_range(tzoffset, TZOFFSET_MIN, TZOFFSET_MAX, 'tzoffset', nil, 1)
    end
    return tzoffset, tzindex
end

-- Historically the timestamp is erroneously considered as
-- "local", not UTC, so it is adjusted by the timezone offset.
-- The new compat behavior treats the timestamp as UTC and
-- preserves it as is (gh-10363).
local function check_and_update_epoch(epoch, offset)
    if not tweaks.datetime_apply_timezone_preserves_timestamp then
        epoch = utc_secs(epoch, offset)
    end
    check_range(epoch, MIN_EPOCH_SECS_VALUE, MAX_EPOCH_SECS_VALUE, 'timestamp',
        nil, 1)
    return epoch
end

-- Create datetime given attribute values from obj.
---Create an object of the `datetime` type from a table of time units.
---
---**Default values:**
---
---* `nsec: 0`
---* `sec: 0`
---* `min: 0`
---* `hour: 0`
---* `day: 1`
---* `year: 1970`
---* `timestamp: 0`
---* `tzoffset: 0`
---* `tz: nil`
---
---If an empty table or no arguments are passed, the `datetime` object with
---the default values corresponding to Unix Epoch is created:
---`1970-01-01T00:00:00Z`.
---
---@param obj? datetime.units table of time units.
---@return datetime datetime_obj the created datetime object.
local function datetime_new(obj)
    if obj == nil then
        return datetime_new_raw(0, 0, 0, 0)
    end
    check_table(obj, 'datetime.new()')

    local y, M, d, ymd = extract_obj_ymd(obj)
    local h, m, s, hms = extract_obj_hms(obj)
    local nsec = extract_obj_nsec(obj)
    local epoch, has_nsec
    epoch, nsec, has_nsec = extract_obj_epoch_and_update_nsec(obj, ymd, hms,
                                                              nsec)
    nsec = nsec or 0

    -- Timestamp case.
    if epoch ~= nil then
        local tzoffset, tzindex = extract_obj_tzoffset_tzindex(obj, epoch)
        tzoffset, tzindex = tzoffset or 0, tzindex or 0
        epoch = check_and_update_epoch(epoch, tzoffset)
        return datetime_new_raw(epoch, nsec, tzoffset, tzindex)
    end

    -- ymd | hms case.
    local dt = DAYS_EPOCH_OFFSET

    -- .year, .month, .day
    if ymd then
        dt = dt_from_ymd(y or 1970, M or 1, d or 1)
    end

    local tzoffset, tzindex = extract_obj_tzoffset_tzindex(obj,
        epoch_from_dt(dt))
    tzoffset, tzindex = tzoffset or 0, tzindex or 0

    -- Only the timezone is provided. The new compat behavior
    -- preserves the zero timestamp, so `new{tz = x}` remains
    -- equivalent to `new{}:set{tz = x}` (gh-10363).
    if not ymd and not hms and not has_nsec and
       tweaks.datetime_apply_timezone_preserves_timestamp then
        return datetime_new_raw(0, 0, tzoffset, tzindex)
    end

    -- .hour, .minute, .second
    local secs = 0
    if hms then
        secs = (h or 0) * 3600 + (m or 0) * 60 + (s or 0)
    end

    return datetime_new_dt(dt, secs, nsec, tzoffset, tzindex)
end

--[[
    Convert to text datetime values

    - datetime will use ISO-8601 format:
        1970-01-01T00:00Z
        2021-08-18T16:57:08.981725+03:00
]]
local function datetime_tostring(self)
    local buff = date_tostr_stash_take()
    local len = builtin.tnt_datetime_to_string(self, buff, TOSTRING_BUFSIZE)
    assert(len < TOSTRING_BUFSIZE)
    local s = ffi.string(buff)
    date_tostr_stash_put(buff)
    return s
end

--[[
    Convert to text interval values of different types

    - depending on a values stored there generic interval
      values may be displayed in the following format:
        +12 secs
        -23 minutes, 0 seconds
        +12 hours, 23 minutes, 1 seconds
        -7 days, -23 hours, -23 minutes, -1 seconds
    - years will be displayed as
        +10 years
    - months will be displayed as:
        +2 months
]]
local function interval_tostring(self)
    check_interval(self, 'datetime.interval.tostring')
    local buff = ival_tostr_stash_take()
    local len = builtin.tnt_interval_to_string(self, buff, IVAL_TOSTRING_BUFSIZE)
    if len < IVAL_TOSTRING_BUFSIZE then
        local s = ffi.string(buff)
        ival_tostr_stash_put(buff)
        return s
    end
    -- slow path - reallocate for a fuller size, and then restart interval_to_string
    ival_tostr_stash_put(buff)
    buff = ffi.new('char[?]', len + 1)
    builtin.tnt_interval_to_string(self, buff, len + 1)
    return ffi.string(buff)
end

-- subtract/addition operation for date object and interval
local function datetime_increment_by(self, direction, ival)
    local rc = builtin.tnt_datetime_increment_by(self, direction, ival)
    if rc == 0 then
        return self
    end
    local operation = direction >= 0 and 'addition' or 'subtraction'
    if rc < 0 then
        error(('%s makes date less than minimum allowed %s'):
                format(operation, MIN_DATE_TEXT), 3)
    else -- rc > 0
        error(('%s makes date greater than maximum allowed %s'):
                format(operation, MAX_DATE_TEXT), 3)
    end
end

local check_ranges = {
    [1] = {'year', MAX_YEAR_RANGE},
    [2] = {'month', MAX_MONTH_RANGE},
    [3] = {'week', MAX_WEEK_RANGE},
    [4] = {'day', MAX_DAY_RANGE},
    [5] = {'hour', MAX_HOUR_RANGE},
    [6] = {'min', MAX_MIN_RANGE},
    [7] = {'sec', MAX_SEC_RANGE},
    [8] = {'nsec', MAX_NSEC_RANGE},
}

local function check_rc(rc, operation, obj)
    -- fast path
    if rc == 0 then
        return obj
    end

    -- slow, error reporting path
    local index = rc < 0 and -rc or rc
    assert(index >= 1 and index <= 8)
    local txt, max = unpack(check_ranges[index])
    local v = obj[txt]
    error(('%s moves value %s of %s out of allowed range [%s, %s]'):
            format(operation, v, txt, -max, max), 3)
end

-- subtract operation when left is date, and right is date
local function datetime_datetime_sub(lhs, rhs)
    local obj = interval_new()
    local rc = builtin.tnt_datetime_datetime_sub(obj, lhs, rhs)
    return check_rc(rc, 'subtraction', obj)
end

-- subtract operation for both left and right operands are intervals
local function interval_interval_sub(lhs, rhs)
    local lobj = interval_decode_args(interval_new_copy(lhs))
    local robj = interval_decode_args(rhs)
    local rc = builtin.tnt_interval_interval_sub(lobj, robj)
    return check_rc(rc, 'subtraction', lobj)
end

-- addition operation for both left and right operands are intervals
local function interval_interval_add(lhs, rhs)
    local lobj = interval_decode_args(interval_new_copy(lhs))
    local robj = interval_decode_args(rhs)
    local rc = builtin.tnt_interval_interval_add(lobj, robj)
    return check_rc(rc, 'addition', lobj)
end

local function date_first(lhs, rhs)
    if is_datetime(rhs) then
        return rhs, lhs
    else
        return lhs, rhs
    end
end

local function error_incompatible(name)
    error(("datetime:%s() - incompatible type of arguments"):
          format(name), 3)
end

--[[
Matrix of subtraction operands eligibility and their result type

|                 | datetime | interval | table    |
+-----------------+----------+----------+----------+
| datetime        | interval | datetime | datetime |
| interval        |          | interval | interval |
| table           |          |          |          |
]]
local function datetime_interval_sub(lhs, rhs)
    check_date_interval(lhs, "operator -")
    check_date_interval_table(rhs, "operator -")
    local left_is_interval = is_table(lhs) or is_interval(lhs)
    local right_is_interval = is_table(rhs) or is_interval(rhs)

    -- left is date, right is interval
    if not left_is_interval and right_is_interval then
        return datetime_increment_by(datetime_new_copy(lhs), -1,
                                     interval_decode_args(rhs))
    -- left is date, right is date
    elseif not left_is_interval and not right_is_interval then
        return datetime_datetime_sub(lhs, rhs)
    -- both left and right are intervals
    elseif left_is_interval and right_is_interval then
        return interval_interval_sub(lhs, rhs)
    else
        error_incompatible("operator -")
    end
end

--[[
Matrix of addition operands eligibility and their result type

|                 | datetime | interval | table    |
+-----------------+----------+----------+----------+
| datetime        |          | datetime | datetime |
| interval        | datetime | interval | interval |
| table           |          |          |          |
]]
local function datetime_interval_add(lhs, rhs)
    lhs, rhs = date_first(lhs, rhs)

    check_date_interval(lhs, "operator +")
    check_interval_table(rhs, "operator +")
    local left_is_interval = is_table(lhs) or is_interval(lhs)
    local right_is_interval = is_table(rhs) or is_interval(rhs)

    -- left is date, right is interval
    if not left_is_interval and right_is_interval then
        local obj = datetime_new_copy(lhs)
        return datetime_increment_by(obj, 1, interval_decode_args(rhs))
    -- both left and right are intervals
    elseif left_is_interval and right_is_interval then
        return interval_interval_add(lhs, rhs)
    else
        error_incompatible("operator +")
    end
end

--[[
    Parse partial ISO-8601 date string

    Accepted formats are:

    Basic      Extended
    20121224   2012-12-24   Calendar date   (ISO 8601)
    2012359    2012-359     Ordinal date    (ISO 8601)
    2012W521   2012-W52-1   Week date       (ISO 8601)
    2012Q485   2012-Q4-85   Quarter date

    Returns pair of constructed datetime object, and length of string
    which has been accepted by parser.
]]
---Parse a partial ISO-8601 date string.
---
---Accepted formats are:
---
---* Calendar date (ISO 8601): `2012-12-24`.
---* Ordinal date (ISO 8601): `2012-359`.
---* Week date (ISO 8601): `2012-W52-1`.
---* Quarter date: `2012-Q4-85`.
---
---@param str string Input string with the date information.
---@return datetime date the parsed datetime object.
---@return number? len the number of parsed characters.
local function datetime_parse_date(str)
    check_str(str, 'datetime.parse_date()')
    local dt = date_dt_stash_take()
    local len = tonumber(builtin.tnt_dt_parse_iso_date(str, #str, dt))
    if len == 0 then
        date_dt_stash_put(dt)
        error(('invalid date format %s'):format(str), 2)
    end
    local d = datetime_new_dt(dt[0])
    date_dt_stash_put(dt)
    return d, tonumber(len)
end

--[[
    datetime parse function for strings in extended iso-8601 format
    assumes to deal with date T time time_zone at once
       date [T] time [ ] time_zone
    Returns constructed datetime object and length of accepted string.
]]
local function datetime_parse_full(str)
    check_str(str, 'datetime.parse()')
    local date = ffi.new(datetime_t)
    local len = builtin.tnt_datetime_parse_full(date, str, #str)
    if len > 0 then
        return date, tonumber(len)
    elseif len == -builtin.TZ_NYI then
        error(("could not parse '%s' - nyi timezone"):format(str))
    elseif len == -builtin.TZ_AMBIGUOUS then
        error(("could not parse '%s' - ambiguous timezone"):format(str))
    else -- len <= 0
        error(("could not parse '%s'"):format(str))
    end
end

--[[
    Parse datetime string given `strptime` like format.
    Returns constructed datetime object and length of accepted string.
]]
local function datetime_parse_format(str, fmt)
    local date = ffi.new(datetime_t)
    local len = builtin.tnt_datetime_strptime(date, str, fmt)
    if len == 0 then
        error(("could not parse '%s' using '%s' format"):format(str, fmt))
    end
    return date, tonumber(len)
end

---Convert an input string with the date and time information into a
---`datetime` object.
---
---The input string should be formatted according to one of the following
---standards:
---
---* ISO 8601.
---* RFC 3339.
---* Extended `strftime` -- see description of the `format()` for details.
---
---By default fields that are not specified are equal to appropriate values
---in a Unix time.
---
---Leap second is supported, see a section leap second.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> datetime.parse('1970-01-01T00:00:00Z')
--- ---
--- - 1970-01-01T00:00:00Z
--- - 20
--- ...
---
--- tarantool> t = datetime.parse('1970-01-01T00:00:00',
---         >     {format = 'iso8601', tzoffset = 180})
---
--- tarantool> t
--- ---
--- - 1970-01-01T00:00:00+0300
--- ...
---
--- tarantool> t = datetime.parse('2017-12-27T18:45:32.999999-05:00',
---         >     {format = 'rfc3339'})
---
--- tarantool> t
--- ---
--- - 2017-12-27T18:45:32.999999-0500
--- ...
---
--- tarantool> T = datetime.parse('Thu Jan  1 03:00:00 1970', {format = '%c'})
---
--- tarantool> T
--- ---
--- - 1970-01-01T03:00:00Z
--- ...
---
--- tarantool> T = datetime.parse('12/31/2020', {format = '%m/%d/%y'})
---
--- tarantool> T
--- ---
--- - 2020-12-31T00:00:00Z
--- ...
---
--- tarantool> T = datetime.parse('1970-01-01T03:00:00.125000000+0300',
---         >     {format = '%FT%T.%f%z'})
---
--- tarantool> T
--- ---
--- - 1970-01-01T03:00:00.125+0300
--- ...
---
--- tarantool> dt = datetime.parse('01:01:01 MSK', {format ='%H:%M:%S %Z'})
---
--- ---
--- ...
---
--- tarantool> dt.year
--- ---
--- - 1970
--- ...
---
--- tarantool> dt.month
--- ---
--- - 1
--- ...
---
--- tarantool> dt.wday
--- ---
--- - 5
--- ...
---
--- tarantool> dt.tz
--- ---
--- - MSK
--- ...
--- ```
---
---@param str string Input string with the date and time information.
---@param obj? { format?: string, tzoffset?: integer, tz?: string } parsing
---    options. `format` may be 'iso8601', 'rfc3339', or a `strptime`-like
---    format string.
---@return datetime date the parsed datetime object.
---@return number? len the number of parsed characters.
local function datetime_parse_from(str, obj)
    check_str(str, 'datetime.parse()')
    local fmt = ''
    local tzoffset
    local tzname

    if obj ~= nil then
        check_table(obj, 'datetime.parse()')
        fmt = obj.format
        tzoffset = obj.tzoffset
        tzname = obj.tz
    end
    check_str_or_nil(fmt, 'datetime.parse()')

    if tzoffset ~= nil then
        local offset = get_timezone(tzoffset, 'tzoffset')
        check_range(offset, TZOFFSET_MIN, TZOFFSET_MAX, 'tzoffset')
    end

    if tzname ~= nil then
        check_str(tzname, 'datetime.parse()')
    end

    if tzoffset and tzname then
        error("ambiguous timezone: both tzoffset and tz are specified")
    end

    local date, len
    if not fmt or fmt == '' or fmt == 'iso8601' or fmt == 'rfc3339' then
        date, len = datetime_parse_full(str)
    else
        date, len = datetime_parse_format(str, fmt)
    end

    -- Override timezone, if it was not specified in a parsed
    -- string.
    if date.tzindex == 0 and date.tzoffset == 0 then
        datetime_set(date, { tzoffset = tzoffset, tz = tzname })

        -- If working in a "preserve timestamp" mode the set
        -- call changes the represented time of day and it should
        -- be adjusted (gh-10363).
        if tweaks.datetime_apply_timezone_preserves_timestamp then
            date.epoch = utc_secs(date.epoch, date.tzoffset)
        end
    end

    return date, len
end

--[[
    Create datetime object representing current time using microseconds
    platform timer and local timezone information.
]]
---Create an object of the `datetime` type with the current date and time.
---
---@return datetime now the current datetime object.
local function datetime_now()
    local d = datetime_new_raw(0, 0, 0, 0)
    builtin.tnt_datetime_now(d)
    return d
end

-- addition or subtraction from date/time of a given interval
-- described via table direction should be +1 or -1
local function datetime_shift(self, o, direction)
    assert(direction == -1 or direction == 1)
    local title = direction > 0 and "datetime.add" or "datetime.sub"
    check_interval_table(o, title)

    return datetime_increment_by(self, direction, interval_decode_args(o))
end

--[[
    dt_dow() returns days of week in range: 1=Monday .. 7=Sunday
    convert it to os.date() wday which is in range: 1=Sunday .. 7=Saturday
]]
local function dow_to_wday(dow)
    return tonumber(dow) % 7 + 1
end

--[[
    Return table in os.date('*t') format, but with timezone
    and nanoseconds
]]
---Convert the information from a `datetime` object into the table format.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> dt = datetime.new {
---   sec = 20,
---   min = 25,
---   hour = 18,
---
---   day = 20,
---   month = 8,
---   year = 2021,
---   tz = 'MAGT',
---   }
--- ---
--- ...
---
--- tarantool> dt:totable()
--- ---
--- - tz: 'MAGT'
---   sec: 20
---   min: 25
---   yday: 232
---   day: 20
---   nsec: 0
---   isdst: false
---   wday: 6
---   tzoffset: 600
---   month: 8
---   year: 2021
---   hour: 18
--- ...
--- ```
---
---@param self datetime
---@return datetime.totable result table with the date and time parameters.
local function datetime_totable(self)
    check_date(self, 'datetime.totable()')
    local dt = local_dt(self)
    local tmp_ival = interval_new()
    local rc = builtin.tnt_datetime_totable(self, tmp_ival)
    assert(rc == true)

    return {
        year = tmp_ival.year,
        month = tmp_ival.month,
        yday = builtin.tnt_dt_doy(dt),
        wday = dow_to_wday(builtin.tnt_dt_dow(dt)),
        day = tmp_ival.day,
        hour = tmp_ival.hour,
        min = tmp_ival.min,
        sec = tmp_ival.sec,
        isdst = datetime_isdst(self),
        nsec = self.nsec,
        tzoffset = self.tzoffset,
        tz = self.tz,
        timestamp = self.timestamp,
    }
end

local function datetime_update_epoch_dt(epoch, dt)
    local secs_day = epoch % SECS_PER_DAY
    return (dt - DAYS_EPOCH_OFFSET) * SECS_PER_DAY + secs_day
end

local function datetime_update_epoch_ymd(epoch, y, M, d)
    local dt = dt_from_ymd(y, M, d)
    return datetime_update_epoch_dt(epoch, dt)
end

local function datetime_update_epoch_hms(epoch, h, m, s)
    local secs_day = epoch - (epoch % SECS_PER_DAY)
    return secs_day + h * 3600 + m * 60 + s
end

---Update the field values in the existing `datetime` object.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> dt = datetime.new {
---   nsec = 123456789,
---
---   sec = 20,
---   min = 25,
---   hour = 18,
---
---   day = 20,
---   month = 8,
---   year = 2021,
---
---   tzoffset  = 180
--- }
---
--- tarantool> dt:set {msec = 567}
--- ---
--- - 2021-08-20T18:25:20.567+0300
--- ...
---
--- tarantool> dt:set {tzoffset = 60}
--- ---
--- - 2021-08-20T18:25:20.567+0100
--- ...
--- ```
---
---@param self datetime
---@param obj datetime.units table of time units.
---@return datetime self the updated datetime object.
function datetime_set(self, obj)
    check_date(self, 'datetime.set()')
    check_table(obj, "datetime.set()")

    local y, M, d, ymd = extract_obj_ymd(obj)
    local h, m, s, hms = extract_obj_hms(obj)
    local nsec = extract_obj_nsec(obj)
    local epoch, has_nsec
    epoch, nsec, has_nsec = extract_obj_epoch_and_update_nsec(obj, ymd, hms,
                                                              nsec, true)

    if epoch ~= nil then
        local tzoffset, tzindex = extract_obj_tzoffset_tzindex(obj, epoch)
        local effective_tzoffset = tzoffset or self.tzoffset
        epoch = check_and_update_epoch(epoch, effective_tzoffset)

        self.epoch = epoch
        self.nsec = nsec
        self.tzoffset = effective_tzoffset
        self.tzindex = tzindex or self.tzindex
        return self
    end

    -- Only the timezone is changed. The new compat behavior
    -- preserves the timestamp, so the epoch is not recalculated
    -- (gh-10363). Time units provided together with a timezone
    -- are still applied to the wall clock time below.
    if not ymd and not hms and not has_nsec and
       tweaks.datetime_apply_timezone_preserves_timestamp then
        local tzoffset, tzindex = extract_obj_tzoffset_tzindex(obj, self.epoch)
        self.tzoffset = tzoffset or self.tzoffset
        self.tzindex = tzindex or self.tzindex
        return self
    end

    local dt = local_dt(self)
    local y0 = ffi.new('int[1]')
    local M0 = ffi.new('int[1]')
    local d0 = ffi.new('int[1]')
    builtin.tnt_dt_to_ymd(dt, y0, M0, d0)
    y0, M0, d0 = y0[0], M0[0], d0[0]

    -- Normalize time to UTC from current timezone,
    -- lsecs is "normalized" time.
    local lsecs = local_secs(self)
    local h0 = math_floor(lsecs / (60 * 60)) % 24
    local m0 = math_floor(lsecs / 60) % 60
    local s0 = lsecs % 60

    -- .year, .month, .day
    if ymd then
        lsecs = datetime_update_epoch_ymd(lsecs, y or y0, M or M0, d or d0)
    end

    -- TODO need to check:
    -- datetime_new() uses epoch_from_dt(dt) as base_epoch, there
    -- h:m:s = 00:00:00. This differ with lsecs as base_epoch
    -- due to h:m:s may have any proper value.
    local tzoffset, tzindex = extract_obj_tzoffset_tzindex(obj, lsecs)

    -- .hour, .minute, .second
    if hms then
        lsecs = datetime_update_epoch_hms(lsecs, h or h0, m or m0, s or s0)
    end

    -- denormalize back to local timezone
    local effective_tzoffset = tzoffset or self.tzoffset
    self.epoch = utc_secs(lsecs, effective_tzoffset)
    self.nsec = nsec or self.nsec
    self.tzoffset = effective_tzoffset
    self.tzindex = tzindex or self.tzindex

    return self
end

local function datetime_strftime(self, fmt)
    check_str(fmt, "datetime.strftime()")
    local buff = date_strf_stash_take()
    local len = builtin.tnt_datetime_strftime(self, buff, STRFTIME_BUFSIZE, fmt)
    if len < STRFTIME_BUFSIZE then
        local s = ffi.string(buff)
        date_strf_stash_put(buff)
        return s
    end
    -- slow path - reallocate for a fuller size, and then restart strftime
    date_strf_stash_put(buff)
    buff = ffi.new('char[?]', len + 1)
    builtin.tnt_datetime_strftime(self, buff, len + 1, fmt)
    return ffi.string(buff)
end

---Convert the standard `datetime` object presentation into a formatted
---string.
---
---The conversion specifications are the same as in the `strftime` function.
---Additional specification for nanoseconds is `%f` which also allows a
---modifier to control the output precision of fractional part: `%5f` (see
---the example below).
---
---If no arguments are set for the method, the default conversions are used:
---`'%FT%T.%f%z'` (see the example below).
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> dt = datetime.new {
--- nsec = 123456789,
---
--- sec = 20,
--- min = 25,
--- hour = 18,
---
--- day = 20,
--- month = 8,
--- year = 2021,
---
--- tzoffset  = 180
--- }
--- ---
--- ...
---
--- tarantool> dt:format('%d.%m.%y %H:%M:%S.%5f')
--- ---
--- - 20.08.21 18:25:20.12345
--- ...
---
--- tarantool> dt:format()
--- ---
--- - 2021-08-20T18:25:20.123456789+0300
--- ...
---
--- tarantool> dt:format('%FT%T.%f%z')
--- ---
--- - 2021-08-20T18:25:20.123456789+0300
--- ...
--- ```
---
---@param self datetime
---@param fmt? string Format string consisting of zero or more conversion
---    specifications and ordinary characters.
---@return string result string with the formatted date and time information.
local function datetime_format(self, fmt)
    check_date(self, 'datetime.format()')
    if fmt ~= nil then
        return datetime_strftime(self, fmt)
    else
        return datetime_tostring(self)
    end
end

local datetime_index_fields = {
    timestamp = function(self) return self.epoch + self.nsec / 1e9 end,

    year = function(self) return builtin.tnt_dt_year(local_dt(self)) end,
    yday = function(self) return builtin.tnt_dt_doy(local_dt(self)) end,
    month = function(self) return builtin.tnt_dt_month(local_dt(self)) end,
    day = function(self)
        return builtin.tnt_dt_dom(local_dt(self))
    end,
    wday = function(self)
        return dow_to_wday(builtin.tnt_dt_dow(local_dt(self)))
    end,
    hour = function(self) return math_floor((local_secs(self) / 3600) % 24) end,
    min = function(self) return math_floor((local_secs(self) / 60) % 60) end,
    sec = function(self) return self.epoch % 60 end,
    usec = function(self) return self.nsec / 1e3 end,
    msec = function(self) return self.nsec / 1e6 end,
    isdst = function(self) return datetime_isdst(self) end,
    tz = function(self) return self:format('%Z') end,
}

local datetime_index_functions = {
    format = datetime_format,
    totable = datetime_totable,
    set = datetime_set,
    ---Modify an existing datetime object by adding values of the input
    ---argument.
    ---
    ---The addition is performed taking `tzdata` into account, when `tzoffset`
    ---or `tz` fields are set.
    ---
    ---**Example #1:**
    ---
    --- ```tarantoolsession
    --- tarantool> dt = datetime.new {
    --- day = 26,
    --- month = 8,
    --- year = 2021,
    --- tzoffset  = 180
    --- }
    --- ---
    --- ...
    ---
    --- tarantool> iv = datetime.interval.new {day = 7}
    --- ---
    --- ...
    ---
    --- tarantool> dt, iv
    --- ---
    --- - 2021-08-26T00:00:00+0300
    --- - +7 days
    --- ...
    ---
    --- tarantool> dt:add(iv)
    --- ---
    --- - 2021-09-02T00:00:00+0300
    --- ...
    ---
    --- tarantool> dt:add{ day = 7 }
    --- ---
    --- - 2021-09-09T00:00:00+0300
    --- ...
    --- ```
    ---
    ---**Example #2:**
    ---
    --- ```tarantoolsession
    --- tarantool> dt = datetime.new {
    --- day = 29,
    --- month = 2,
    --- year = 2020
    --- }
    --- ---
    --- ...
    ---
    --- tarantool> dt:add{month = 1, adjust = 'none'}
    --- ---
    --- - 2020-03-29T00:00:00Z
    --- ...
    ---
    --- tarantool> dt = datetime.new {
    --- day = 29,
    --- month = 2,
    --- year = 2020
    --- }
    --- ---
    --- ...
    ---
    --- tarantool> dt:add{month = 1, adjust = 'last'}
    --- ---
    --- - 2020-03-31T00:00:00Z
    --- ...
    ---
    --- tarantool> dt = datetime.new {
    --- day = 31,
    --- month = 1,
    --- year = 2020
    --- }
    --- ---
    --- ...
    ---
    --- tarantool> dt:add{month = 1, adjust = 'excess'}
    --- ---
    --- - 2020-03-02T00:00:00Z
    --- ...
    --- ```
    ---
    ---@param self datetime
    ---@param obj datetime.interval.units | datetime.interval the input
    ---    interval object or an equivalent table.
    ---@return datetime self the modified datetime object.
    add = function(self, obj) return datetime_shift(self, obj, 1) end,
    ---Modify an existing datetime object by subtracting values of the input
    ---argument.
    ---
    ---The subtraction is performed taking `tzdata` into account, when
    ---`tzoffset` or `tz` fields are set.
    ---
    ---**Example:**
    ---
    --- ```tarantoolsession
    --- tarantool> dt = datetime.new {
    --- day = 26,
    --- month = 8,
    --- year = 2021,
    --- tzoffset  = 180
    --- }
    --- ---
    --- ...
    ---
    --- tarantool> iv = datetime.interval.new {day = 5}
    --- ---
    --- ...
    ---
    --- tarantool> dt, iv
    --- ---
    --- - 2021-08-26T00:00:00+0300
    --- - +5 days
    --- ...
    ---
    --- tarantool> dt:sub(iv)
    --- ---
    --- - 2021-08-21T00:00:00+0300
    --- ...
    ---
    --- tarantool> dt:sub{ day = 1 }
    --- ---
    --- - 2021-08-20T00:00:00+0300
    --- ...
    --- ```
    ---
    ---@param self datetime
    ---@param obj datetime.interval.units | datetime.interval the input
    ---    interval object or an equivalent table.
    ---@return datetime self the modified datetime object.
    sub = function(self, obj) return datetime_shift(self, obj, -1) end,
}

local function datetime_index(self, key)
    local handler_field = datetime_index_fields[key]
    if handler_field ~= nil then
        return handler_field(self)
    end
    return datetime_index_functions[key]
end

ffi.metatype(datetime_t, {
    __tostring = datetime_tostring,
    __eq = datetime_eq,
    __lt = datetime_lt,
    __le = datetime_le,
    __sub = datetime_interval_sub,
    __add = datetime_interval_add,
    __index = datetime_index,
})

---Convert the information from an `interval` object into the table format.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> iv = datetime.interval.new{month = 1, adjust = 'last'}
--- ---
--- ...
--- tarantool> iv:totable()
--- ---
--- - adjust: last
--- sec: 0
--- nsec: 0
--- day: 0
--- week: 0
--- hour: 0
--- month: 1
--- year: 0
--- min: 0
--- ...
--- ```
---
---@param self datetime.interval
---@return datetime.interval.totable? result table with the date and time
---    parameters.
local function interval_totable(self)
    if not is_interval(self) then
        return error(("interval.totable(): expected interval, but received "..
                     type(self)), 2)
    end
    local adjust = {'excess', 'none', 'last'}
    return {
        year = self.year,
        month = self.month,
        week = self.week,
        day = self.day,
        hour = self.hour,
        min = self.min,
        sec = self.sec,
        nsec = self.nsec,
        adjust = adjust[tonumber(self.adjust) + 1],
    }
end

local function interval_cmp(lhs, rhs, is_raising)
    if not is_interval(lhs) or not is_interval(rhs) then
        if is_raising then
            error('incompatible types for interval comparison', 3)
        else
            return nil
        end
    end
    local tags = {
        'year', 'month', 'week', 'day',
        'hour', 'min', 'sec', 'nsec'
    }
    for _, key in pairs(tags) do
        local diff = lhs[key] - rhs[key]
        if diff ~= 0 then
            return diff
        end
    end
    return 0
end

local function interval_eq(lhs, rhs)
    return interval_cmp(lhs, rhs, false) == 0
end

local function interval_lt(lhs, rhs)
    return interval_cmp(lhs, rhs, true) < 0
end

local function interval_le(lhs, rhs)
    return interval_cmp(lhs, rhs, true) <= 0
end

local interval_index_fields = {
    usec = function(self) return math_floor(self.nsec / 1e3) end,
    msec = function(self) return math_floor(self.nsec / 1e6) end,
}

local interval_index_functions = {
    totable = interval_totable,
    __serialize = interval_tostring,
}

local function interval_index(self, key)
    local handler_field = interval_index_fields[key]
    return handler_field ~= nil and handler_field(self) or
           interval_index_functions[key]
end

ffi.metatype(interval_t, {
    __tostring = interval_tostring,
    __eq = interval_eq,
    __lt = interval_lt,
    __le = interval_le,
    __sub = datetime_interval_sub,
    __add = datetime_interval_add,
    __index = interval_index,
})

local interval_mt = {
    new     = interval_new,
    is_interval = is_interval,
}

---@class datetime.interval.module
---@field new fun(obj?: datetime.interval.units): datetime.interval
---@field is_interval fun(o: any): boolean
local interval = setmetatable(interval_mt, interval_mt)

---@class datetime.tz
---@field [integer|string] string|integer
local TZ = tz

---# Builtin `datetime` module
---
---The `datetime` module provides support for the `datetime` and `interval`
---data types. It allows creating the date and time values either via the
---object interface or via parsing string values conforming to the ISO-8601
---standard.
---
---**Datetime and interval arithmetic**
---
---The `datetime` module enables creating objects of two types:
---`datetime` and `interval`.
---
---To shift datetime values, use `datetime_object:add()` or
---`datetime_object:sub()`, or the overloaded `+` and `-` operators. The
---methods modify the current object, while the operators return a new
---object.
---
---In an interval operation each component is calculated from the largest
---(`year`) to the smallest (`nsec`): `year`, `month`, `week`, `day`,
---`hour`, `min`, `sec`, `nsec`.
---
---* `interval + interval` returns an interval.
---* `interval - interval` returns an interval.
---* `datetime + interval` returns a datetime.
---* `datetime - datetime` returns an interval.
---* `datetime + datetime` is unsupported.
---* `interval + datetime` returns a datetime.
---
---An untyped table can be used instead of a typed interval or datetime
---operand if the left operand is a typed object.
---
---**Example:**
---
---```tarantoolsession
---tarantool> datetime.new({tz = 'MSK'}) - datetime.new({tz = 'UTC'})
---    ---
---    - -180 minutes
---    ...
---```
---
---**Datetime and interval comparison**
---
---Use the standard Lua relational operators (`==`, `~=`, `>`, `<`, `>=`,
---`<=`). They use the `__eq`, `__lt` and `__le` metamethods.
---
---**Example:**
---
---```tarantoolsession
---tarantool> dt1 = datetime.new({year = 2010})
---tarantool> dt2 = datetime.new({year = 2024})
---tarantool> dt1 == dt2
---    ---
---    - false
---    ...
---tarantool> dt1 < dt2
---    ---
---    - true
---    ...
---```
---
---**Leap second**
---
---Leap seconds are supported at the most basic level:
---
---* `datetime.parse()` parses an input string with 60 seconds.
---* `datetime.new()` and `datetime_object:set()` accept a table with
---  `sec = 60`.
---
---**Example:**
---
---```tarantoolsession
---tarantool> datetime.parse('23:12:60', {format = '%H:%M:%S'})
---    ---
---    - 1970-01-01T23:13:00Z
---    - 8
---    ...
---tarantool> datetime.new({sec = 60})
---    ---
---    - 1970-01-01T00:01:00Z
---    ...
---```
---
---Parsing a leap second with the `rfc3339` or `iso8601` format is not
---supported and produces an error.
---
---**Time zones**
---
---Tarantool uses the IANA Time Zone Database for timezone support.
---Each datetime object has `tz`, `tzoffset` and `isdst` fields:
---
---* `tz` is a timezone name or abbreviation, available via `datetime.TZ`.
---* `tzoffset` is calculated automatically from the Olson rule when `tz`
---  is set, or can be set manually.
---* `isdst` shows whether daylight saving time is applicable.
---
---The fields `tz` and `tzoffset` can be set in `datetime.new()`,
---`datetime.parse()` and `datetime_object:set()`.
---
---**Example:**
---
---```tarantoolsession
---tarantool> require('datetime').parse('2004-06-01T00:00 Europe/Moscow').isdst
---    ---
---    - true
---    ...
---tarantool> datetime.TZ['Europe/Moscow']
---    ---
---    - 947
---    ...
---```
---
---**Limitations**
---
---* The supported date range is from `-5879610-06-22` to
---  `+5879611-07-11`.
---* Some ancient timezones had offsets with seconds; the module uses
---  minutes, so precision may be lost for such timestamps.
local datetime_module = {
    new         = datetime_new,
    interval    = interval,
    now         = datetime_now,
    parse       = datetime_parse_from,
    parse_date  = datetime_parse_date,
    is_datetime = is_datetime,
    TZ          = TZ,
}
return datetime_module
