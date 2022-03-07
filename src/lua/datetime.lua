local ffi = require('ffi')
local buffer = require('buffer')

--[[
    `c-dt` library functions handles properly both positive and negative `dt`
    values, where `dt` is a number of dates since Rata Die date (0001-01-01).

    `c-dt` uses 32-bit integer number to store `dt` values, so range of
    suported dates is limited to dates from -5879610-06-22 (INT32_MIN) to
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
size_t tnt_dt_parse_iso_date (const char *str, size_t len, dt_t *dt);
size_t tnt_dt_parse_iso_zone_lenient(const char *str, size_t len, int *offset);

/* Tarantool datetime functions - datetime.c */
size_t tnt_datetime_to_string(const struct datetime * date, char *buf,
                              ssize_t len);
size_t tnt_datetime_strftime(const struct datetime *date, char *buf,
                             uint32_t len, const char *fmt);
size_t tnt_datetime_parse_full(struct datetime *date, const char *str,
                               size_t len, int32_t offset);
size_t tnt_datetime_strptime(struct datetime *date, const char *buf,
                             const char *fmt);
void   tnt_datetime_now(struct datetime *now);

/* Tarantool interval support functions */
size_t
tnt_interval_to_string(const struct datetime_interval *, char *, ssize_t);

]]

local builtin = ffi.C
local math_modf = math.modf
local math_floor = math.floor

-- Unix, January 1, 1970, Thursday
local DAYS_EPOCH_OFFSET = 719163
local SECS_PER_DAY      = 86400
local SECS_EPOCH_OFFSET = DAYS_EPOCH_OFFSET * SECS_PER_DAY
local NANOS_PER_SEC     = 1e9
local TOSTRING_BUFSIZE  = 48
local IVAL_TOSTRING_BUFSIZE = 96
local STRFTIME_BUFSIZE  = 128

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
local AVERAGE_DAYS_MONTH = AVERAGE_DAYS_YEAR / 12
local AVERAGE_WEEK_YEAR = AVERAGE_DAYS_YEAR / 7
local INT_MAX = 2147483647
local INT_MIN = -2147483648
-- -5879610-06-22
local MIN_DATE_TEXT = ('%d-%02d-%02d'):format(MIN_DATE_YEAR, MIN_DATE_MONTH,
                                              MIN_DATE_DAY)
-- 5879611-07-11
local MAX_DATE_TEXT = ('%d-%02d-%02d'):format(MAX_DATE_YEAR, MAX_DATE_MONTH,
                                              MAX_DATE_DAY)
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

local datetime_t = ffi.typeof('struct datetime')
local interval_t = ffi.typeof('struct datetime_interval')

local function is_interval(o)
    return ffi.istype(interval_t, o)
end

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

local function check_integer(v, message)
    if v == nil then
        return
    end
    if type(v) ~= 'number' or v % 1 ~= 0 then
        error(('%s: integer value expected, but received %s'):
              format(message, type(v)), 4)
    end
end

local function check_str_or_nil(s, message)
    if s ~= nil and type(s) ~= 'string' then
        return error(("%s: expected string, but received %s"):
                     format(message, type(s)), 2)
    end
end

-- range may be of a form of pair {from, to} or
-- tuple {fom, to, -1 in extra}
-- -1 is a special value (so far) used for days only
local function check_range(v, from, to, txt, extra)
    if type(v) ~= 'number' then
        error(('numeric value expected, but received %s'):
              format(type(v)), 3)
    end
    if extra == v or (v >= from and v <= to) then
        return
    end
    if extra == nil then
        error(('value %d of %s is out of allowed range [%d, %d]'):
              format(v, txt, from, to), 3)
    else
        error(('value %d of %s is out of allowed range [%d, %d..%d]'):
              format(v, txt, extra, from, to), 3)
    end
end

local function check_dt(dt_v, direction, v, unit)
    if dt_v >= INT_MIN and dt_v <= INT_MAX then
        return
    end
    local operation = direction >= 0 and 'addition' or 'subtraction'
    local title = unit ~= nil and ('%s of %d %s'):format(operation, v, unit) or
                                  operation
    if dt_v < INT_MIN then
        error(('%s makes date less than minimum allowed %s'):
                format(title, MIN_DATE_TEXT), 3)
    elseif dt_v > INT_MAX then
        error(('%s makes date greater than maximum allowed %s'):
                format(title, MAX_DATE_TEXT), 3)
    else
        assert(false)
    end
end

local function dt_from_ymd_checked(y, M, d)
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
    if v > -max and v < max then
        return v
    end
    error(('value %s of %s is out of allowed range [%s, %s]'):
            format(v, txt, -max, max), 4)
end

local function bool2int(b)
    return b and 1 or 0
end

local function normalize_nsec(secs, nsec)
    if nsec < 0 or nsec >= NANOS_PER_SEC then
        secs = secs + math_floor(nsec / NANOS_PER_SEC)
        nsec = nsec % NANOS_PER_SEC
    end
    return secs, nsec
end

local adjust_xlat = {
    none = builtin.DT_LIMIT,
    last = builtin.DT_SNAP,
    excess = builtin.DT_EXCESS,
}

local function interval_init(year, month, sec, nsec, adjust)
    return ffi.new(interval_t, sec, nsec, month, year, adjust)
end

local function interval_new_copy(obj)
    return interval_init(obj.year, obj.month, obj.sec, obj.nsec, obj.adjust)
end


local function interval_decode_args(obj)
    if is_interval(obj) then
        return obj
    end
    local year = checked_max_value(obj.year, MAX_YEAR_RANGE, 'year', 0)
    local month = checked_max_value(obj.month, MAX_MONTH_RANGE, 'month', 0)
    local adjust = adjust_xlat[obj.adjust] or DEF_DT_ADJUST

    local secs = 0
    secs = secs + (7 * SECS_PER_DAY) *
                  checked_max_value(obj.week, MAX_WEEK_RANGE, 'week', 0)
    secs = secs + SECS_PER_DAY *
                  checked_max_value(obj.day, MAX_DAY_RANGE, 'day', 0)
    secs = secs + (60 * 60) *
                  checked_max_value(obj.hour, MAX_HOUR_RANGE, 'hour', 0)
    secs = secs + 60 * checked_max_value(obj.min, MAX_MIN_RANGE, 'min', 0)
    local sec = checked_max_value(obj.sec, MAX_SEC_RANGE, 'sec', 0)
    check_integer(sec, 'sec')

    local nsec = checked_max_value(obj.nsec, MAX_NSEC_RANGE, 'nsec')
    local usec = checked_max_value(obj.usec, MAX_USEC_RANGE, 'usec')
    local msec = checked_max_value(obj.msec, MAX_MSEC_RANGE, 'msec')
    local count_usec = bool2int(nsec ~= nil) + bool2int(usec ~= nil) +
                       bool2int(msec ~= nil)
    if count_usec > 1 then
        error('only one of nsec, usec or msecs may be defined '..
                'simultaneously', 3)
    end
    secs = secs + sec
    nsec = (msec or 0) * 1e6 + (usec or 0) * 1e3 + (nsec or 0)

    return interval_init(year, month, secs, nsec, adjust)
end

local function interval_new(obj)
    if obj == nil then
        return ffi.new(interval_t, 0, 0, 0, 0, DEF_DT_ADJUST)
    end
    check_table(obj, 'interval.new()')
    return interval_decode_args(obj)
end

local function nyi(msg)
    error(("Not yet implemented : '%s'"):format(msg), 3)
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
local function parse_tzoffset(str)
    local offset = ffi.new('int[1]')
    local len = builtin.tnt_dt_parse_iso_zone_lenient(str, #str, offset)
    if len ~= #str then
        error(('invalid time-zone format %s'):format(str), 3)
    end
    return offset[0]
end

local function datetime_new_raw(epoch, nsec, tzoffset)
    local dt_obj = ffi.new(datetime_t)
    dt_obj.epoch = epoch
    dt_obj.nsec = nsec
    dt_obj.tzoffset = tzoffset
    dt_obj.tzindex = 0
    return dt_obj
end

local function datetime_new_copy(obj)
    return datetime_new_raw(obj.epoch, obj.nsec, obj.tzoffset)
end

local function datetime_new_dt(dt, secs, nanosecs, offset)
    secs = secs or 0
    nanosecs = nanosecs or 0
    offset = offset or 0
    local epoch = (dt - DAYS_EPOCH_OFFSET) * SECS_PER_DAY
    return datetime_new_raw(epoch + secs - offset * 60, nanosecs, offset)
end

local function get_timezone(offset, msg)
    if type(offset) == 'number' then
        return offset
    elseif type(offset) == 'string' then
        return parse_tzoffset(offset)
    else
        error(('%s: string or number expected, but received %s'):
              format(msg, offset), 3)
    end
end

-- create datetime given attribute values from obj
local function datetime_new(obj)
    if obj == nil then
        return datetime_new_raw(0, 0, 0)
    end
    check_table(obj, 'datetime.new()')

    local ymd = false
    local hms = false
    local dt = DAYS_EPOCH_OFFSET

    local y = obj.year
    if y ~= nil then
        check_range(y, MIN_DATE_YEAR, MAX_DATE_YEAR, 'year')
        ymd = true
    end
    local M = obj.month
    if M ~= nil then
        check_range(M, 1, 12, 'month')
        ymd = true
    end
    local d = obj.day
    if d ~= nil then
        check_range(d, 1, 31, 'day', -1)
        ymd = true
    end
    local h = obj.hour
    if h ~= nil then
        check_range(h, 0, 23, 'hour')
        hms = true
    end
    local m = obj.min
    if m ~= nil then
        check_range(m, 0, 59, 'min')
        hms = true
    end
    local s = obj.sec
    if s ~= nil then
        check_range(s, 0, 60, 'sec')
        hms = true
    end

    local nsec, usec, msec = obj.nsec, obj.usec, obj.msec
    local count_usec = bool2int(nsec ~= nil) + bool2int(usec ~= nil) +
                       bool2int(msec ~= nil)
    if count_usec > 0 then
        if count_usec > 1 then
            error('only one of nsec, usec or msecs may be defined '..
                  'simultaneously', 2)
        end
        if usec ~= nil then
            check_range(usec, 0, 1e6, 'usec')
            nsec = usec * 1e3
        elseif msec ~= nil then
            check_range(msec, 0, 1e3, 'msec')
            nsec = msec * 1e6
        else
            check_range(nsec, 0, 1e9, 'nsec')
        end
    else
        nsec = 0
    end
    local ts = obj.timestamp
    if ts ~= nil then
        if ymd then
            error('timestamp is not allowed if year/month/day provided', 2)
        end
        if hms then
            error('timestamp is not allowed if hour/min/sec provided', 2)
        end
        local fraction
        s, fraction = math_modf(ts)
        -- if there are separate nsec, usec, or msec provided then
        -- timestamp should be integer
        if count_usec == 0 then
            nsec = fraction * 1e9
        elseif fraction ~= 0 then
            error('only integer values allowed in timestamp '..
                  'if nsec, usec, or msecs provided', 2)
        end
        hms = true
    end

    local offset = obj.tzoffset
    if offset ~= nil then
        offset = get_timezone(offset, 'tzoffset')
        -- at the moment the range of known timezones is UTC-12:00..UTC+14:00
        -- https://en.wikipedia.org/wiki/List_of_UTC_time_offsets
        check_range(offset, -720, 840, 'tzoffset')
    end

    if obj.tz ~= nil then
        nyi('tz')
    end

    -- .year, .month, .day
    if ymd then
        y = y or 1970
        M = M or 1
        d = d or 1
        if d < 0 then
            d = builtin.tnt_dt_days_in_month(y, M)
        elseif d > 28 then
            local day_in_month = builtin.tnt_dt_days_in_month(y, M)
            if d > day_in_month then
                error(('invalid number of days %d in month %d for %d'):
                    format(d, M, y), 3)
            end
        end
        dt = dt_from_ymd_checked(y, M, d)
    end

    -- .hour, .minute, .second
    local secs = 0
    if hms then
        secs = (h or 0) * 3600 + (m or 0) * 60 + (s or 0)
    end

    return datetime_new_dt(dt, secs, nsec, offset or 0)
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

local function datetime_increment_by(self, direction, ival)
    -- operations with intervals should be done using local human dates
    -- not UTC dates, thus normalize to local timezone before operations.
    local dt = local_dt(self)
    local secs, nsec = local_secs(self), self.nsec
    local offset = self.tzoffset

    local is_ym_updated = false
    local years, months = ival.year, ival.month
    local seconds, nanoseconds = ival.sec, ival.nsec
    local adjust = ival.adjust or DEF_DT_ADJUST

    if years ~= 0 then
        check_range(years, MIN_DATE_YEAR, MAX_DATE_YEAR, 'years')
        local approx_dt = dt + direction * years * AVERAGE_DAYS_YEAR
        check_dt(approx_dt, direction, years, 'years')
        -- tnt_dt_add_years() not handle properly DT_SNAP or DT_LIMIT mode
        -- so use tnt_dt_add_months() as a work-around
        dt = builtin.tnt_dt_add_months(dt, direction * years * 12, adjust)
        is_ym_updated = true
    end
    if months ~= 0 then
        local new_dt = dt + direction * months * AVERAGE_DAYS_MONTH
        check_dt(new_dt, direction, months, 'months')
        dt = builtin.tnt_dt_add_months(dt, direction * months, adjust)
        is_ym_updated = true
    end
    if is_ym_updated then
        secs = (dt - DAYS_EPOCH_OFFSET) * SECS_PER_DAY + secs % SECS_PER_DAY
    end

    if seconds ~= 0 then
        secs = secs + direction * seconds
    end

    if nanoseconds ~= 0 then
        nsec = nsec + direction * nanoseconds
    end

    secs, self.nsec = normalize_nsec(secs, nsec)
    check_dt((secs + SECS_EPOCH_OFFSET) / SECS_PER_DAY, direction)
    self.epoch = utc_secs(secs, offset)
    return self
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
        local obj = interval_new()
        obj.sec, obj.nsec = lhs.epoch - rhs.epoch, lhs.nsec - rhs.nsec
        return obj
    -- both left and right are intervals
    elseif left_is_interval and right_is_interval then
        local lobj = interval_decode_args(interval_new_copy(lhs))
        local robj = interval_decode_args(rhs)
        lobj.year = lobj.year - robj.year
        lobj.month = lobj.month - robj.month
        -- do not normalize nsec yet - leave till the operation with date
        lobj.sec, lobj.nsec = lobj.sec - robj.sec, lobj.nsec - robj.nsec
        return lobj
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
        local lobj = interval_decode_args(interval_new_copy(lhs))
        local robj = interval_decode_args(rhs)
        lobj.year = lobj.year + robj.year
        lobj.month = lobj.month + robj.month
        -- do not normalize nsec yet - leave till the operation with date
        lobj.sec, lobj.nsec = lobj.sec + robj.sec, lobj.nsec + robj.nsec
        return lobj
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
local function datetime_parse_full(str, tzoffset)
    check_str(str, 'datetime.parse()')
    local date = ffi.new(datetime_t)
    local len = builtin.tnt_datetime_parse_full(date, str, #str, tzoffset)
    if len == 0 then
        error(("could not parse '%s'"):format(str))
    end
    return date, tonumber(len)
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

local function datetime_parse_from(str, obj)
    check_str(str, 'datetime.parse()')
    local fmt = ''
    local offset

    if obj ~= nil then
        check_table(obj, 'datetime.parse()')
        fmt = obj.format
        offset = obj.tzoffset
    end
    check_str_or_nil(fmt, 'datetime.parse()')

    if offset ~= nil then
        offset = get_timezone(offset, 'tzoffset')
        check_range(offset, -720, 840, 'tzoffset')
    end
    if obj and obj.tz ~= nil then
        nyi('tz')
    end

    if not fmt or fmt == '' or fmt == 'iso8601' or fmt == 'rfc3339' then
        return datetime_parse_full(str, offset or 0)
    else
        return datetime_parse_format(str, fmt)
    end
end

--[[
    Create datetime object representing current time using microseconds
    platform timer and local timezone information.
]]
local function datetime_now()
    local d = datetime_new_raw(0, 0, 0)
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
local function datetime_totable(self)
    check_date(self, 'datetime.totable()')
    local secs = local_secs(self) -- hour:minute should be in local timezone
    local dt = local_dt(self)

    return {
        year = builtin.tnt_dt_year(dt),
        month = builtin.tnt_dt_month(dt),
        yday = builtin.tnt_dt_doy(dt),
        day = builtin.tnt_dt_dom(dt),
        wday = dow_to_wday(builtin.tnt_dt_dow(dt)),
        hour = math_floor((secs / 3600) % 24),
        min = math_floor((secs / 60) % 60),
        sec = secs % 60,
        isdst = false,
        nsec = self.nsec,
        tzoffset = self.tzoffset,
    }
end

local function datetime_update_dt(self, dt, new_offset)
    local epoch = local_secs(self)
    local secs_day = epoch % SECS_PER_DAY
    epoch = (dt - DAYS_EPOCH_OFFSET) * SECS_PER_DAY + secs_day
    self.epoch = utc_secs(epoch, new_offset)
end

local function datetime_ymd_update(self, y, M, d, new_offset)
    if d < 0 then
        d = builtin.tnt_dt_days_in_month(y, M)
    elseif d > 28 then
        local day_in_month = builtin.tnt_dt_days_in_month(y, M)
        if d > day_in_month then
            error(('invalid number of days %d in month %d for %d'):
                  format(d, M, y), 3)
        end
    end
    local dt = dt_from_ymd_checked(y, M, d)
    datetime_update_dt(self, dt, new_offset)
end

local function datetime_hms_update(self, h, m, s, new_offset)
    local epoch = local_secs(self)
    local secs_day = epoch - (epoch % SECS_PER_DAY)
    self.epoch = utc_secs(secs_day + h * 3600 + m * 60 + s, new_offset)
end

local function datetime_set(self, obj)
    check_date(self, 'datetime.set()')
    check_table(obj, "datetime.set()")

    local ymd = false
    local hms = false

    local dt = local_dt(self)
    local y0 = ffi.new('int[1]')
    local M0 = ffi.new('int[1]')
    local d0 = ffi.new('int[1]')
    builtin.tnt_dt_to_ymd(dt, y0, M0, d0)
    y0, M0, d0 = y0[0], M0[0], d0[0]

    local y = obj.year
    if y ~= nil then
        check_range(y, MIN_DATE_YEAR, MAX_DATE_YEAR, 'year')
        ymd = true
    end
    local M = obj.month
    if M ~= nil then
        check_range(M, 1, 12, 'month')
        ymd = true
    end
    local d = obj.day
    if d ~= nil then
        check_range(d, 1, 31, 'day', -1)
        ymd = true
    end

    local lsecs = local_secs(self)
    local h0 = math_floor(lsecs / (24 * 60)) % 24
    local m0 = math_floor(lsecs / 60) % 60
    local sec0 = lsecs % 60

    local h = obj.hour
    if h ~= nil then
        check_range(h, 0, 23, 'hour')
        hms = true
    end
    local m = obj.min
    if m ~= nil then
        check_range(m, 0, 59, 'min')
        hms = true
    end
    local sec = obj.sec
    if sec ~= nil then
        check_range(sec, 0, 60, 'sec')
        hms = true
    end

    local nsec, usec, msec = obj.nsec, obj.usec, obj.msec
    local count_usec = bool2int(nsec ~= nil) + bool2int(usec ~= nil) +
                       bool2int(msec ~= nil)
    if count_usec > 0 then
        if count_usec > 1 then
            error('only one of nsec, usec or msecs may be defined '..
                  'simultaneously', 2)
        end
        if usec ~= nil then
            check_range(usec, 0, 1e6, 'usec')
            self.nsec = usec * 1e3
        elseif msec ~= nil then
            check_range(msec, 0, 1e3, 'msec')
            self.nsec = msec * 1e6
        elseif nsec ~= nil then
            check_range(nsec, 0, 1e9, 'nsec')
            self.nsec = nsec
        end
    end

    local ts = obj.timestamp
    if ts ~= nil then
        if ymd then
            error('timestamp is not allowed if year/month/day provided', 2)
        end
        if hms then
            error('timestamp is not allowed if hour/min/sec provided', 2)
        end
        local sec_int, fraction
        sec_int, fraction = math_modf(ts)
        -- if there is one of nsec, usec, msec provided
        -- then ignore fraction in timestamp
        -- otherwise - use nsec, usec, or msec
        if count_usec == 0 then
            nsec = fraction * 1e9
        else
            error('only integer values allowed in timestamp '..
                  'if nsec, usec, or msecs provided', 2)
        end

        self.epoch = sec_int
        self.nsec = nsec

        return self
    end

    local offset = obj.tzoffset
    if offset ~= nil then
        offset = get_timezone(offset, 'tzoffset')
        check_range(offset, -720, 840, 'tzoffset')
    end
    offset = offset or self.tzoffset

    if obj.tz ~= nil then
        nyi('tz')
    end

    -- .year, .month, .day
    if ymd then
        y = y or y0
        M = M or M0
        d = d or d0
        -- check_ymd_range(y, M, d)
        datetime_ymd_update(self, y, M, d, offset)
    end

    -- .hour, .minute, .second
    if hms then
        datetime_hms_update(self, h or h0, m or m0, sec or sec0, offset)
    end

    self.tzoffset = offset

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
    isdst = function(_) return false end,
}

local datetime_index_functions = {
    format = datetime_format,
    totable = datetime_totable,
    set = datetime_set,
    add = function(self, obj) return datetime_shift(self, obj, 1) end,
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

local function total_secs(self)
    return self.sec + self.nsec / 1e9
end

local interval_index_fields = {
    usec = function(self) return math_floor(self.nsec / 1e3) end,
    msec = function(self) return math_floor(self.nsec / 1e6) end,

    week = function(self)
        return math_modf(total_secs(self) / (7 * SECS_PER_DAY))
    end,
    day = function(self)
        return math_modf(total_secs(self) / SECS_PER_DAY)
    end,
    hour = function(self)
        return math_modf(total_secs(self) / (60 * 60))
    end,
    min =  function(self)
        return math_modf(total_secs(self) / 60)
    end,
}

local interval_index_functions = {
    __serialize = interval_tostring,
}

local function interval_index(self, key)
    local handler_field = interval_index_fields[key]
    return handler_field ~= nil and handler_field(self) or
           interval_index_functions[key]
end

ffi.metatype(interval_t, {
    __tostring = interval_tostring,
    __sub = datetime_interval_sub,
    __add = datetime_interval_add,
    __index = interval_index,
})

local interval_mt = {
    new     = interval_new,
}

return setmetatable(
    {
        new         = datetime_new,
        interval    = setmetatable(interval_mt, interval_mt),
        now         = datetime_now,
        parse       = datetime_parse_from,
        parse_date  = datetime_parse_date,
        is_datetime = is_datetime,
    }, {}
)
