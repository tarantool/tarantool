--[[
https://github.com/tarantool/tarantool/issues/8887
]]

local datetime = require("datetime")
local msgpack = require("msgpack")
local luzer = require("luzer")

-- `0x7ffffffffffff` is a maximum integer in `long long`, however
-- this number is not representable in `double` and the nearest
-- number representable in `double` is `0x7ffffffffffffc00`.
--
-- 1. https://www.lua.org/manual/5.1/manual.html#lua_Integer
-- 2. https://www.lua.org/manual/5.3/manual.html#lua_Integer
local MAX_INT64 =  0x7ffffffffffffc00
local MIN_INT64 = -0x8000000000000000

-- See https://www.tarantool.io/en/doc/latest/reference/reference_lua/datetime/interval_new/
local function new_itv(fdp)
    local INT_MIN = MIN_INT64
    local INT_MAX = MAX_INT64
    return {
        nsec      = fdp:consume_integer(INT_MIN, INT_MAX),
        usec      = fdp:consume_integer(INT_MIN, INT_MAX),
        msec      = fdp:consume_integer(INT_MIN, INT_MAX),
        sec       = fdp:consume_integer(INT_MIN, INT_MAX),
        min       = fdp:consume_integer(INT_MIN, INT_MAX),
        hour      = fdp:consume_integer(INT_MIN, INT_MAX),
        day       = fdp:consume_integer(INT_MIN, INT_MAX),
        week      = fdp:consume_integer(INT_MIN, INT_MAX),
        month     = fdp:consume_integer(INT_MIN, INT_MAX),
        year      = fdp:consume_integer(INT_MIN, INT_MAX),
        adjust    = fdp:consume_integer(INT_MIN, INT_MAX),
    }
end

local function TestOneInput(buf)
    local fdp = luzer.FuzzedDataProvider(buf)
    local itv = new_itv(fdp)
    local ok, dt_itv = pcall(datetime.interval.new, itv)
    if not ok then
        return
    end
    local encoded_itv
    ok, encoded_itv = pcall(msgpack.encode, dt_itv)
    assert(ok)
    local res
    ok, res = pcall(msgpack.decode, encoded_itv)
    assert(ok)
    assert(itv == res)
end

local args = {
    artifact_prefix = "msgpack_itv_",
}
luzer.Fuzz(TestOneInput, nil, args)
