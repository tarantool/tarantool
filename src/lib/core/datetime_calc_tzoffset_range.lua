--[[
This script purpose is to calculate MIN_TZOFFSET and
MAX_TZOFFSET for gh-12417 [1].

The idea is to read zone records from tzdata base [2],
searching for special first line of the record.

#Zone       NAME      STDOFF   RULES FORMAT [UNTIL]
Z     Pacific/Palau   -15:2:4  -       LMT  1844 D 31

We search for line with 'old' local mean time (LMT), where
offsets from UTC may be out of modern standard range
of [-12:00, +14:00] or [-720m, 840m].

1. https://github.com/tarantool/tarantool/issues/12417
2. ftp://ftp.iana.org/tz/tz-how-to.html
]]--
local TZDATA_FILE = '/usr/share/zoneinfo/tzdata.zi'

local ZONE = 'Z'
local NAME = '[%a/_]+'
local STDOFF = '-?[%d:]+'
local RULES = '-'
local FORMAT = 'LMT'
local UNTIL_YEAR = '%d+'

local ZONE_LINE = '^'..ZONE..'%s.*$'
local TARGET_LINE = '^'..ZONE..'%s+('..NAME..')%s+('..STDOFF..')%s+'..
    RULES..'%s+'..FORMAT..'%s+('..UNTIL_YEAR..').*$'

local MIN_TZOFFSET_STD = -12 * 60
local MAX_TZOFFSET_STD = 14 * 60

local log = require('log')
local json = require('json')
local datetime = require('datetime')

local dump = false
local min_offs, max_offs = 0, 0
local found = {}

local stat = {}
local function inc(name)
    stat[name] = (stat[name] or 0) + 1
end

local function trunc(x)
    if x < 0 then return math.ceil(x) end
    return math.floor(x)
end

local function handle_line(line)
    inc('lines')
    if not string.match(line, ZONE_LINE) then return end
    inc('zone_records')

    local matches = {string.match(line, TARGET_LINE)}
    if dump then
        log.info(json.encode({matches = matches, line = line}))
    end

    if #matches ~= 3 then return end
    inc('target_records')

    local tz, offs_time, lmt_y = matches[1], matches[2], tonumber(matches[3])

    local tcomp = string.split(offs_time, ':')
    if #tcomp < 1 or #tcomp > 3 then
        inc('unexpected_stdoff')
        return
    end

    local h = tonumber(tcomp[1])
    local m = tonumber(tcomp[2] or '0')
    local s = tonumber(tcomp[3] or '0')
    local sign = h < 0 and -1 or 1
    h = h * sign

    local d = datetime.new({hour = h, min = m, sec = s})
    local offs = sign * trunc(d.timestamp / 60)

    if MIN_TZOFFSET_STD <= offs and offs <= MAX_TZOFFSET_STD then return end
    inc('found')
    table.insert(found, {tz = tz, tzoffset = offs, year = lmt_y})
    if min_offs > offs then min_offs = offs end
    if max_offs < offs then max_offs = offs end
end

local file = assert(io.open(TZDATA_FILE, 'r'))
local line = file:read('*line')
while line ~= nil do
    handle_line(line)
    line = file:read('*line')
end

log.info('stat = %s', json.encode(stat))
if dump then log.info('found = %s', json.encode(found)) end

local items = ''
for _, e in ipairs(found) do
    items = items..('    {tz = \'%s\', tzoffset = %d, year = %d},\n')
        :format(e.tz, e.tzoffset, e.year)
end

log.info([[

#define MAX_TZOFFSET (%d)
#define MIN_TZOFFSET (%d)

MAX_TZOFFSET = %d
MIN_TZOFFSET = %d

local HISTORICAL_TZOFFSET_OUT_OF_STD = {
%s}
]], max_offs, min_offs, max_offs, min_offs, items)
