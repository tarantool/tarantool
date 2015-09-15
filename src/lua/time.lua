-- init.lua -- internal file
local time = {}
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

time.realtime = C.clock_realtime
time.monotonic = C.clock_monotonic
time.proc = C.clock_process
time.thread = C.clock_thread

time.realtime64 = C.clock_realtime64
time.monotonic64 = C.clock_monotonic64
time.proc64 = C.clock_process64
time.thread64 = C.clock_thread64

time.time = time.realtime
time.time64 = time.realtime64

time.bench = function(fun, ...)
    local overhead = time.proc()
    overhead = time.proc() - overhead
    local start_time = time.proc()
    local res = {0, fun(...)}
    res[1] = time.proc() - start_time - overhead, res
    return res
end

time.bench64 = function(fun, ...)
    local overhead = time.proc64()
    overhead = time.proc64() - overhead
    local start_time = time.proc64()
    local res = {0, fun(...)}
    res[1] = time.proc64() - start_time - overhead, res
    return res
end

return time
