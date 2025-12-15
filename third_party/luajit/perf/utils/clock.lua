local ffi = require('ffi')

ffi.cdef[[
struct timespec {
  long tv_sec; /* Seconds. */
  long tv_nsec; /* Nanoseconds. */
};

int clock_gettime(int clockid, struct timespec *tp);
]]

local C = ffi.C

-- Wall clock.
local CLOCK_REALTIME = 0
-- CPU time consumed by the process.
local CLOCK_PROCESS_CPUTIME_ID = 2

-- All functions below returns the corresponding elapsed time in
-- seconds.
local M = {}

local timespec = ffi.new('struct timespec[1]')

function M.realtime()
  C.clock_gettime(CLOCK_REALTIME, timespec)
  return tonumber(timespec[0].tv_sec) + tonumber(timespec[0].tv_nsec) / 1e9
end

function M.process_cputime()
  C.clock_gettime(CLOCK_PROCESS_CPUTIME_ID, timespec)
  return tonumber(timespec[0].tv_sec) + tonumber(timespec[0].tv_nsec) / 1e9
end

return M
