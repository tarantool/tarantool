-- clock.lua -- internal file
local clock = {}
local ffi = require('ffi')

ffi.cdef[[
    double clock_realtime(void);
    double clock_monotonic(void);
    double clock_process(void);
    double clock_thread(void);
    uint64_t clock_realtime64(void);
    uint64_t clock_monotonic64(void);
    uint64_t clock_process64(void);
    uint64_t clock_thread64(void);
]]

local C = ffi.C

clock.realtime = C.clock_realtime
clock.monotonic = C.clock_monotonic
clock.proc = C.clock_process
clock.thread = C.clock_thread

clock.realtime64 = C.clock_realtime64
clock.monotonic64 = C.clock_monotonic64
clock.proc64 = C.clock_process64
clock.thread64 = C.clock_thread64

clock.time = clock.realtime
clock.time64 = clock.realtime64

clock.bench = function(fun, ...)
    local overhead = clock.proc()
    overhead = clock.proc() - overhead
    local start_time = clock.proc()
    local res = {0, fun(...)}
    res[1] = clock.proc() - start_time - overhead
    return res
end

return clock
