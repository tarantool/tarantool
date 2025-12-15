local M = {}

local jutil = require('jit.util')

local function getlast_traceno()
  return misc.getmetrics().jit_trace_num
end

-- Convert addr to positive value if needed.
local function canonicalize_address(addr)
  if addr < 0 then addr = addr + 2 ^ 32 end
  return addr
end

-- Some storage is needed to avoid functions and traces being
-- collected.
local recfuncs = {}
local last_i = 0
-- This function generates a table of functions with heavy mcode
-- payload with tab arithmetic to fill the mcode area from the
-- one trace mcode by some given size. This size is usually big
-- enough, because we want to check long jump side exits from some
-- traces.
-- Assumes, that the maxmcode and maxtrace options are set to be
-- sure, that we can produce such amount of mcode.
function M.fillmcode(trace_from, size)
  local mcode, addr_from = jutil.tracemc(trace_from)
  assert(mcode, 'the #1 argument should be an existing trace number')
  addr_from = canonicalize_address(addr_from)
  local required_diff = size + #mcode

  -- Marker to check that traces are not flushed.
  local maxtraceno = getlast_traceno()
  local FLUSH_ERR = 'Traces are flushed, check your maxtrace, maxmcode options'
  local last_addr = addr_from

  -- Addresses of traces may increase or decrease depending on OS,
  -- so use absolute diff.
  while math.abs(last_addr - addr_from) < required_diff do
    last_i = last_i + 1
    -- This is quite a heavy workload (though it doesn't look like
    -- one at first). Each load from a table is type guarded. Each
    -- table lookup (for both stores and loads) is guarded for
    -- table <hmask> value and the presence of the metatable. The
    -- code below results in ~8Kb of mcode for ARM64 and MIPS64 in
    -- practice.
    local fname = ('fillmcode[%d]'):format(last_i)
    recfuncs[last_i] = assert(loadstring(([[
      return function(src)
        local p = %d
        local tmp = { }
        local dst = { }
        -- XXX: use 5 as a stop index to reduce LLEAVE (leaving
        -- loop in root trace) errors due to hotcount collisions.
        for i = 1, 5 do
          tmp.a = src.a * p   tmp.j = src.j * p   tmp.s = src.s * p
          tmp.b = src.b * p   tmp.k = src.k * p   tmp.t = src.t * p
          tmp.c = src.c * p   tmp.l = src.l * p   tmp.u = src.u * p
          tmp.d = src.d * p   tmp.m = src.m * p   tmp.v = src.v * p
          tmp.e = src.e * p   tmp.n = src.n * p   tmp.w = src.w * p
          tmp.f = src.f * p   tmp.o = src.o * p   tmp.x = src.x * p
          tmp.g = src.g * p   tmp.p = src.p * p   tmp.y = src.y * p
          tmp.h = src.h * p   tmp.q = src.q * p   tmp.z = src.z * p
          tmp.i = src.i * p   tmp.r = src.r * p

          dst.a = tmp.z + p   dst.j = tmp.q + p   dst.s = tmp.h + p
          dst.b = tmp.y + p   dst.k = tmp.p + p   dst.t = tmp.g + p
          dst.c = tmp.x + p   dst.l = tmp.o + p   dst.u = tmp.f + p
          dst.d = tmp.w + p   dst.m = tmp.n + p   dst.v = tmp.e + p
          dst.e = tmp.v + p   dst.n = tmp.m + p   dst.w = tmp.d + p
          dst.f = tmp.u + p   dst.o = tmp.l + p   dst.x = tmp.c + p
          dst.g = tmp.t + p   dst.p = tmp.k + p   dst.y = tmp.b + p
          dst.h = tmp.s + p   dst.q = tmp.j + p   dst.z = tmp.a + p
          dst.i = tmp.r + p   dst.r = tmp.i + p
        end
        dst.tmp = tmp
        return dst
      end
    ]]):format(last_i), fname), ('Syntax error in function %s'):format(fname))()
    -- XXX: FNEW is NYI, hence loop recording fails at this point.
    -- The recording is aborted on purpose: the whole loop
    -- recording might lead to a very long trace error (via return
    -- to a lower frame), or a trace with lots of side traces. We
    -- need neither of this, but just a bunch of traces filling
    -- the available mcode area.
    local function tnew(p)
      return {
        a = p + 1, f = p + 6,  k = p + 11, p = p + 16, u = p + 21, z = p + 26,
        b = p + 2, g = p + 7,  l = p + 12, q = p + 17, v = p + 22,
        c = p + 3, h = p + 8,  m = p + 13, r = p + 18, w = p + 23,
        d = p + 4, i = p + 9,  n = p + 14, s = p + 19, x = p + 24,
        e = p + 5, j = p + 10, o = p + 15, t = p + 20, y = p + 25,
      }
    end
    -- Each function call produces a trace (see the template for
    -- the function definition above).
    recfuncs[last_i](tnew(last_i))
    local last_traceno = getlast_traceno()
    if last_traceno < maxtraceno then
      error(FLUSH_ERR)
    end

    -- Calculate the address of the last trace start.
    maxtraceno = last_traceno
    local _
    _, last_addr = jutil.tracemc(last_traceno)
    if not last_addr then
      error(FLUSH_ERR)
    end
    last_addr = canonicalize_address(last_addr)
  end
end

return M
