local ffi = require('ffi')
local cdt = ffi.C

ffi.cdef [[

    typedef int dt_t;
    
    // dt_core.h
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

    dt_t     dt_from_rdn     (int n);
    dt_t     dt_from_yd      (int y, int d);
    dt_t     dt_from_ymd     (int y, int m, int d);
    dt_t     dt_from_yqd     (int y, int q, int d);
    dt_t     dt_from_ywd     (int y, int w, int d);

    void     dt_to_yd        (dt_t dt, int *y, int *d);
    void     dt_to_ymd       (dt_t dt, int *y, int *m, int *d);
    void     dt_to_yqd       (dt_t dt, int *y, int *q, int *d);
    void     dt_to_ywd       (dt_t dt, int *y, int *w, int *d);

    int      dt_rdn          (dt_t dt);
    dt_dow_t dt_dow          (dt_t dt);

    // dt_parse_iso.h
    size_t dt_parse_iso_date          (const char *str, size_t len, dt_t *dt);

    size_t dt_parse_iso_time          (const char *str, size_t len, int *sod, int *nsec);
    size_t dt_parse_iso_time_basic    (const char *str, size_t len, int *sod, int *nsec);
    size_t dt_parse_iso_time_extended (const char *str, size_t len, int *sod, int *nsec);

    size_t dt_parse_iso_zone          (const char *str, size_t len, int *offset);
    size_t dt_parse_iso_zone_basic    (const char *str, size_t len, int *offset);
    size_t dt_parse_iso_zone_extended (const char *str, size_t len, int *offset);
    size_t dt_parse_iso_zone_lenient  (const char *str, size_t len, int *offset);

    // dt_tm.h
    dt_t    dt_from_struct_tm  (const struct tm *tm);
    void    dt_to_struct_tm    (dt_t dt, struct tm *tm);

    // <asm-generic/posix_types.h>
    typedef long            __kernel_long_t;
    typedef unsigned long   __kernel_ulong_t;
    // /usr/include/x86_64-linux-gnu/bits/types/time_t.h
    typedef long            time_t;


    // <time.h>
    typedef __kernel_long_t	__kernel_time_t;
    typedef __kernel_long_t	__kernel_suseconds_t;

    struct timespec {
        __kernel_time_t	        tv_sec;     /* seconds */
        long                    tv_nsec;    /* nanoseconds */
    };

    struct timeval {
        __kernel_time_t	        tv_sec;	    /* seconds */
        __kernel_suseconds_t    tv_usec;    /* microseconds */
    };

    struct timezone {
        int	tz_minuteswest;     /* minutes west of Greenwich */
        int	tz_dsttime;	        /* type of dst correction */
    };

    // /usr/include/x86_64-linux-gnu/sys/time.h
    typedef struct timezone * __timezone_ptr_t;

    /* Get the current time of day and timezone information,
       putting it into *TV and *TZ.  If TZ is NULL, *TZ is not filled.
       Returns 0 on success, -1 on errors.

       NOTE: This form of timezone information is obsolete.
       Use the functions and variables declared in <time.h> instead.  */
    int gettimeofday (struct timeval *__tv, struct timezone * __tz);

    // /usr/include/x86_64-linux-gnu/bits/types/struct_tm.h
    /* ISO C `broken-down time' structure.  */
    struct tm
    {
        int tm_sec;	        /* Seconds.	[0-60] (1 leap second) */
        int tm_min;	        /* Minutes.	[0-59] */
        int tm_hour;        /* Hours.	[0-23] */
        int tm_mday;        /* Day.		[1-31] */
        int tm_mon;	        /* Month.	[0-11] */
        int tm_year;        /* Year	- 1900.  */
        int tm_wday;        /* Day of week.	[0-6] */
        int tm_yday;        /* Days in year.[0-365]	*/
        int tm_isdst;       /* DST.		[-1/0/1]*/

        long int tm_gmtoff; /* Seconds east of UTC.  */
        const char *tm_zone;/* Timezone abbreviation.  */
    };

    // <time.h>
    /* Return the current time and put it in *TIMER if TIMER is not NULL.  */
    time_t time (time_t *__timer);

    /* Format TP into S according to FORMAT.
    Write no more than MAXSIZE characters and return the number
    of characters written, or 0 if it would exceed MAXSIZE.  */
    size_t strftime (char * __s, size_t __maxsize, const char * __format,
                     const struct tm * __tp);

    /* Parse S according to FORMAT and store binary time information in TP.
    The return value is a pointer to the first unparsed character in S.  */
    char *strptime (const char * __s, const char * __fmt, struct tm *__tp);

    /* Return the `struct tm' representation of *TIMER in UTC,
    using *TP to store the result.  */
    struct tm *gmtime_r (const time_t * __timer, struct tm * __tp);

    /* Return the `struct tm' representation of *TIMER in local time,
    using *TP to store the result.  */
    struct tm *localtime_r (const time_t * __timer, struct tm * __tp);

    /* Return a string of the form "Day Mon dd hh:mm:ss yyyy\n"
    that is the representation of TP in this format.  */
    char *asctime (const struct tm *__tp);

    /* Equivalent to `asctime (localtime (timer))'.  */
    char *ctime (const time_t *__timer);

]]

local native = ffi.C

local SECS_PER_DAY     = 86400
local NANOS_PER_SEC    = 1000000000LL
local MIN_UNIT_YEARS   = -10000LL
local MAX_UNIT_YEARS   = 10000LL
local MIN_UNIT_MONTHS  = -120000LL
local MAX_UNIT_MONTHS  = 120000LL
local MIN_UNIT_WEEKS   = -521775LL
local MAX_UNIT_WEEKS   = 521775LL
local MIN_UNIT_DAYS    = -3652425LL
local MAX_UNIT_DAYS    = 3652425LL
local MIN_UNIT_HOURS   = -87658200LL
local MAX_UNIT_HOURS   = 87658200LL
local MIN_UNIT_MINUTES = -5259492000LL
local MAX_UNIT_MINUTES = 5259492000LL
local MIN_UNIT_SECONDS = -315569520000LL
local MAX_UNIT_SECONDS = 315569520000LL
local MIN_UNIT_MILLIS  = -315569520000000LL
local MAX_UNIT_MILLIS  = 315569520000000LL
local MIN_UNIT_MICROS  = -315569520000000000LL
local MAX_UNIT_MICROS  = 315569520000000000LL

local MIN_RATA_DIE_DAY    = 1LL            -- 0001-01-01
local MAX_RATA_DIE_DAY    = 352059LL       -- 9999-12-31

local MIN_RANGE           = 86400          -- 0LL01-01-01T00:00:00Z
local MAX_RANGE           = 315537983999LL -- 9LL99-12-31T23:59:59Z
local UNIX_EPOCH          = 62135683200LL  -- 1LL70-01-01T00:00:00Z
local MIN_EPOCH_SEC       = -62135596800LL -- 0LL01-01-01T00:00:00Z
local MAX_EPOCH_SEC       = 253402300799LL -- 9LL99-12-31T23:59:59Z

-- c-dt/dt_config.h

-- Chronological Julian Date, January 1, 4713 BC, Monday
local DT_JULIAN_4713BC_OFFSET = -1721425LL

-- Network Time Protocol (NTP), January 1, 1900, Monday
local DT_NTP_1900_OFFSET = 693596LL

-- Unix, January 1, 1970, Thursday
local DT_EPOCH_1970_OFFSET = 719163LL

-- Rata Die, January 1, 0001, Monday (as Day 1)
local DT_RATADIE_OFFSET = 0LL

local DT_SECS_EPOCH_OFFSET = (DT_EPOCH_1970_OFFSET - DT_NTP_1900_OFFSET) * SECS_PER_DAY

local function valid_epoch_sec(s)
    return (s >= MIN_EPOCH_SEC) and (s <= MAX_EPOCH_SEC)
end

local datetime_t = ffi.typeof('struct t_datetime_tz')
local duration_t = ffi.typeof('struct t_datetime_duration')

local function duration_new()
    local delta = ffi.new(duration_t)
    return delta
end

local function adjusted_secs(dt)
    return dt.secs - dt.offset * 60
end

local function datetime_sub(lhs, rhs)
    local s1 = adjusted_secs(lhs)
    local s2 = adjusted_secs(rhs)
    local d = duration_new()
    d.secs = s2 - s1
    d.nsec = rhs.nsec - lhs.nsec
    if d.nsec < 0 then
        d.secs = d.secs - 1
        d.nsec = d.nsec + NANOS_PER_SEC
    end
    return d
end

local function datetime_add(lhs, rhs)
    local s1 = adjusted_secs(lhs)
    local s2 = adjusted_secs(rhs)
    local d = duration_new()
    d.secs = s2 + s1
    d.nsec = rhs.nsec + lhs.nsec
    if d.nsec >= NANOS_PER_SEC then
        d.secs = d.secs + 1
        d.nsec = d.nsec - NANOS_PER_SEC
    end
    return d
end

local function datetime_eq(lhs, rhs)
    -- we usually don't need to check nullness
    -- but tarantool console will call is checking for equality to nil
    if rhs == nil then
        return false
    end
    -- FIXME - timezone?
    return (lhs.secs == rhs.secs) and (lhs.nsec == rhs.nsec)
end


local function datetime_lt(lhs, rhs)
    -- FIXME - timezone?
    return (lhs.secs < rhs.secs) or
           (lhs.secs == rhs.secs and lhs.nsec < rhs.nsec)
end

local function datetime_le(lhs, rhs)
    -- FIXME - timezone?
    return (lhs.secs <= rhs.secs) or
           (lhs.secs == rhs.secs and lhs.nsec <= rhs.nsec)
end

--[[ local function datetime_tostring(self)
    return string.format('DateTime:{secs: %d. nsec: %d, offset:%d}',
                         self.secs, self.nsec, self.offset)
end ]]

local function datetime_serialize(self)
    -- Allow YAML, MsgPack and JSON to dump objects with sockets
    return { secs = self.secs, nsec = self.nsec, tz = self.offset }
end

--[[ local function duration_tostring(self)
    return string.format('Duration:{secs: %d. nsec: %d}', self.secs, self.nsec)
end ]]

local function duration_serialize(self)
    -- Allow YAML and JSON to dump objects with sockets
    return { secs = self.secs, nsec = self.nsec }
end

local datetime_mt = {
    -- __tostring = datetime_tostring,
    __serialize = datetime_serialize,
    __eq = datetime_eq,
    __lt = datetime_lt,
    __le = datetime_le,
    __sub = datetime_sub,
    __add = datetime_add,

    nanoseconds = function(self)
        return tonumber(self.secs*NANOS_PER_SEC + self.nsec)
    end,
    microseconds = function(self)
        return tonumber(self.secs*1e6 + self.nsec*1e3)
    end,
    seconds = function(self)
        return tonumber(self.secs + self.nsec*1e3)
    end,
    minutes = function(self)
        return tonumber((self._ticks/(1e6*60))%60)
    end,
    hours = function(self)
        return tonumber(self._ticks/(1e6*60*60))
    end,

}

local duration_mt = {
    -- __tostring = duration_tostring,
    __serialize = duration_serialize,
    __eq = datetime_eq,
    __lt = datetime_lt,
    __le = datetime_le,
}

local function datetime_new_raw(secs, nsec, offset)
    local dt_obj = ffi.new(datetime_t)
    dt_obj.secs = secs
    dt_obj.nsec = nsec
    dt_obj.offset = offset
    return dt_obj
end

local function local_rd(o)
    return math.floor(o.secs / SECS_PER_DAY) + DT_EPOCH_1970_OFFSET
end

local function local_dt(o)
    return cdt.dt_from_rdn(local_rd(o))
end

local function mk_timestamp(dt, sp, fp, offset)
    local epochV = dt ~= nil and (cdt.dt_rdn(dt) - DT_EPOCH_1970_OFFSET) * SECS_PER_DAY or 0
    local spV = sp ~= nil and sp or 0
    local fpV = fp ~= nil and fp or 0
    local ofsV = offset ~= nil and offset or 0
    return datetime_new_raw (epochV + spV - ofsV * 60, fpV, ofsV)
end

-- create @datetime_t given object @o fields
local function datetime_new(o)
    if o == nil then
        return datetime_new_raw(0, 0, 0)
    end
    local secs = 0
    local nsec = 0
    local offset = 0
    local easy_way = false
    local y, M, d, ymd
    y, M, d, ymd = 0, 0, 0, false

    local h, m, s, frac, hms
    h, m, s, frac, hms = 0, 0, 0, 0, false

    local dt = 0

    for key, value in pairs(o) do
        local handlers = {
            epoch = function(v)
                secs = v + UNIX_EPOCH
                easy_way = true
            end,

            secs = function(v)
                secs = v
                easy_way = true
            end,

            nsec = function(v)
                nsec = v
                easy_way = true
            end,

            offset = function (v)
                offset = v
                easy_way = true
            end,

            year = function(v)
                assert(v > 0 and v < 10000)
                y = v
                ymd = true
            end,

            month = function(v)
                assert(v > 0 and v < 12 )
                M = v
                ymd = true
            end,

            day = function(v)
                assert(v > 0 and v < 32)
                d = v
                ymd = true
            end,

            hour = function(v)
                assert(v >= 0 and v < 24)
                h = v
                hms = true
            end,

            minute = function(v)
                assert(v >= 0 and v < 60)
                m = v
                hms = true
            end,

            second = function(v)
                assert(v >= 0 and v < 61)
                frac = v % 1
                if frac then
                    s = v - (v % 1)
                else
                    s = v
                end
                hms = true
            end,

            -- tz offset in minutes
            tz = function(v)
                assert(v >= 0 and v <= 720)
                offset = v
            end
        }
        handlers[key](value)
    end

    -- .sec, .nsec, .offset
    if easy_way then
        return datetime_new_raw(secs, nsec, offset)
    end

    -- .year, .month, .day
    if ymd then
        dt = dt + cdt.dt_from_ymd(y, M, d)
    end

    -- .hour, .minute, .second
    if hms then
        secs = h * 3600 + m * 60 + s
    end

    return mk_timestamp(dt, secs, frac, offset)
end


-- simple parse functions:
-- parse_date/parse_time/parse_zone

--[[
    Basic      Extended
    20121224   2012-12-24   Calendar date   (ISO 8601)
    2012359    2012-359     Ordinal date    (ISO 8601)
    2012W521   2012-W52-1   Week date       (ISO 8601)
    2012Q485   2012-Q4-85   Quarter date
]]

local function parse_date(str)
    local dt = ffi.new('dt_t[1]')
    local rc = cdt.dt_parse_iso_date(str, #str, dt)
    assert(rc > 0)
    return mk_timestamp(dt[0])
end

--[[
    Basic               Extended
    T12                 N/A
    T1230               T12:30
    T123045             T12:30:45
    T123045.123456789   T12:30:45.123456789
    T123045,123456789   T12:30:45,123456789

    The time designator [T] may be omitted.
]]
local function parse_time(str)
    local sp = ffi.new('int[1]')
    local fp = ffi.new('int[1]')
    local rc = cdt.dt_parse_iso_time(str, #str, sp, fp)
    assert(rc > 0)
    return mk_timestamp(nil, sp[0], fp[0])
end

--[[
    Basic    Extended
    Z        N/A
    ±hh      N/A
    ±hhmm    ±hh:mm
]]
local function parse_zone(str)
    local offset = ffi.new('int[1]')
    local rc = cdt.dt_parse_iso_zone(str, #str, offset)
    assert(rc > 0)
    return mk_timestamp(nil, nil, nil, offset[0])
end


--[[
    aggregated parse functions
    assumes to deal with date T time time_zone
    at once

    date [T] time [ ] time_zone
]]
local function parse_str(str)
    local dt = ffi.new('dt_t[1]')
    local len = #str
    local n = cdt.dt_parse_iso_date(str, len, dt)
    local dt_ = dt[0]
    if n == 0 or len == n then
        return mk_timestamp(dt_)
    end

    str = str:sub(tonumber(n) + 1)

    local ch = str:sub(1,1)
    if ch ~= 't' and ch ~= 'T' and ch ~= ' ' then
        return mk_timestamp(dt_)
    end

    str = str:sub(2)
    len = #str

    local sp = ffi.new('int[1]')
    local fp = ffi.new('int[1]')
    local n = cdt.dt_parse_iso_time(str, len, sp, fp)
    if n == 0 then
        return mk_timestamp(dt_)
    end
    local sp_ = sp[0]
    local fp_ = fp[0]
    if len == n then
        return mk_timestamp(dt_, sp_, fp_)
    end

    str = str:sub(tonumber(n) + 1)

    if str:sub(1,1) == ' ' then
        str = str:sub(2)
    end

    len = #str

    local offset = ffi.new('int[1]')
    n = cdt.dt_parse_iso_zone(str, len, offset)
    if n == 0 then
        return mk_timestamp(dt_, sp_, fp_)
    end
    return mk_timestamp(dt_, sp_, fp_, offset[0])
end

local function datetime_from(o)
    if o == nil or type(o) == 'table' then
        return datetime_new(o)
    elseif type(o) == 'string' then
        return parse_str(o)
    end
end

local function local_now()
    local p_tv = ffi.new ' struct timeval [1] '
    local rc = native.gettimeofday(p_tv, nil)
    assert(rc == 0)

    local secs = p_tv[0].tv_sec
    local nsec = p_tv[0].tv_usec * 1000

    local p_time = ffi.new 'time_t[1]'
    local p_tm = ffi.new 'struct tm[1]'
    native.time(p_time)
    native.localtime_r(p_time, p_tm)
    -- local dt = cdt.dt_from_struct_tm(p_tm)
    local ofs = p_tm[0].tm_gmtoff / 60 -- convert seconds to minutes

    return datetime_new_raw(secs, nsec, ofs) -- FIXME
end

local function asctime(o)
    assert(ffi.typeof(o) == datetime_t)
    local p_tm = ffi.new 'struct tm[1]'
    cdt.dt_to_struct_tm(local_dt(o), p_tm)
    return ffi.string(native.asctime(p_tm))
end

local function ctime(o)
    assert(ffi.typeof(o) == datetime_t)
    local p_time = ffi.new 'time_t[1]'
    p_time[0] = o.secs
    return ffi.string(native.ctime(p_time))
end

local function strftime(fmt, o)
    assert(ffi.typeof(o) == datetime_t)
    local sz = 50
    local buff = ffi.new('char[?]', sz)
    local p_tm = ffi.new 'struct tm[1]'
    cdt.dt_to_struct_tm(local_dt(o), p_tm)
    native.strftime(buff, sz, fmt, p_tm)
    return ffi.string(buff)
end

-- strftime may be redirected to datetime:fmt("format")
local function datetime_fmt()
end


ffi.metatype(duration_t, duration_mt)
ffi.metatype(datetime_t, datetime_mt)

return setmetatable(
    {
        datetime = datetime_new,
        delta = duration_new,

        parse = parse_str,
        parse_date = parse_date,
        parse_time = parse_time,
        parse_zone = parse_zone,
        fmt = datetime_fmt,

        now = local_now,
    -- strptime = strptime;
        strftime = strftime,
        asctime = asctime,
        ctime = ctime,
    }, {
        __call = function(self, ...) return datetime_from(...) end
    }
)
