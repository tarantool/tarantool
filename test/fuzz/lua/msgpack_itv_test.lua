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
    adjust = fdp:consume_integer(INT_MIN, INT_MAX),
    day = fdp:consume_integer(INT_MIN, INT_MAX),
    hour = fdp:consume_integer(INT_MIN, INT_MAX),
    min = fdp:consume_integer(INT_MIN, INT_MAX),
    month = fdp:consume_integer(INT_MIN, INT_MAX),
    msec = fdp:consume_integer(INT_MIN, INT_MAX),
    nsec = fdp:consume_integer(INT_MIN, INT_MAX),
    sec = fdp:consume_integer(INT_MIN, INT_MAX),
    usec = fdp:consume_integer(INT_MIN, INT_MAX),
    week = fdp:consume_integer(INT_MIN, INT_MAX),
    year = fdp:consume_integer(INT_MIN, INT_MAX),
  }
end

local itv_field = {
  "adjust",
  "day",
  "hour",
  "min",
  "month",
  "msec",
  "nsec",
  "sec",
  "usec",
  "week",
  "year",
}

local function is_equal_itv(itv1, itv2)
  local res = true
  for _, field_name in ipairs(itv_field) do
    local value1 = itv1[field_name]
    local value2 = itv2[field_name]
    local err_msg = ("itv1.%s ~= itv2.% (%s ~ %s)\n"):format(
      field_name, field_name, value1, value2)
    if value1 ~= value2 then
      io.stderr:write(err_msg)
      res = false
    end
  end
  return res
end

local function TestOneInput(buf)
  local fdp = luzer.FuzzedDataProvider(buf)
  local itv = new_itv(fdp)
  -- `datetime.interval.new()` may raise an error when interval
  -- field value is out of supported range.
  local ok, dt_itv = pcall(datetime.interval.new, itv)
  if not ok then
    return -1
  end
  local encoded_itv = msgpack.encode(dt_itv)
  local res = msgpack.decode(encoded_itv)
  assert(is_equal_itv(itv, res))
end

local args = {
  artifact_prefix = "msgpack_itv_",
}
luzer.Fuzz(TestOneInput, nil, args)
