-- clock.lua -- internal file

---The `clock` module returns time values derived from the POSIX / C
---[CLOCK_GETTIME](http://pubs.opengroup.org/onlinepubs/9699919799/functions/clock_getres.html)
---function or equivalent. Most functions in the module return
---a number of seconds; functions whose names end in "64" return a 64-bit
---number of nanoseconds.
---
---@class clock
---@field realtime fun(): number Get the wall clock time in seconds.
---
---    The wall clock time. Derived from C function
---    `clock_gettime(CLOCK_REALTIME)`.
---
---    Returns seconds since epoch (1970-01-01 00:00:00), adjusted.
---
---    **Example:**
---
---    ```lua
---    -- This will print an approximate number of years since 1970.
---    clock = require('clock')
---    print(clock.realtime() / (365*24*60*60))
---    ```
---@field time fun(): number Get the wall clock time in seconds.
---
---    Equivalent to `clock.realtime()`.
---
---    **Example:**
---
---    ```lua
---    clock = require('clock')
---    print(clock.time() / (365*24*60*60))
---    ```
---@field realtime64 fun(): ffi.cdata* Get the wall clock time in nanoseconds.
---
---    The wall clock time. Derived from C function
---    `clock_gettime(CLOCK_REALTIME)`.
---
---    Returns nanoseconds since epoch (1970-01-01 00:00:00), adjusted.
---
---    **Example:**
---
---    ```lua
---    clock = require('clock')
---    print(clock.realtime64() / (1e9 * 365*24*60*60))
---    ```
---@field time64 fun(): ffi.cdata* Get the wall clock time in nanoseconds.
---
---    Equivalent to `clock.realtime64()`.
---
---    **Example:**
---
---    ```lua
---    clock = require('clock')
---    print(clock.time64() / (1e9 * 365*24*60*60))
---    ```
---@field monotonic fun(): number Get the monotonic time in seconds.
---
---    Derived from C function `clock_gettime(CLOCK_MONOTONIC)`.
---    Monotonic time is similar to wall clock time but is not affected by
---    changes to or from daylight saving time, or by changes done by a
---    user.
---
---    This is the best function to use with benchmarks that need to
---    calculate elapsed time.
---
---    Returns seconds since the last time the computer was booted.
---
---    **Example:**
---
---    ```lua
---    -- This will print seconds since the start.
---    clock = require('clock')
---    print(clock.monotonic())
---    ```
---@field monotonic64 fun(): ffi.cdata* Get the monotonic time in nanoseconds.
---
---    Derived from C function `clock_gettime(CLOCK_MONOTONIC)`.
---
---    Returns nanoseconds since the last time the computer was booted.
---
---    **Example:**
---
---    ```lua
---    -- This will print nanoseconds since the start.
---    clock = require('clock')
---    print(clock.monotonic64())
---    ```
---@field proc fun(): number Get the processor time in seconds.
---
---    Derived from C function
---    `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`.
---
---    This is the best function to use with benchmarks that need to
---    calculate how much time has been spent within a CPU.
---
---    Return the CPU time consumed by the process, in seconds.
---
---    **Example:**
---
---    ```lua
---    -- This will print CPU seconds consumed by the process.
---    clock = require('clock')
---    print(clock.proc())
---    ```
---@field proc64 fun(): ffi.cdata* Get the processor time in nanoseconds.
---
---    Derived from C function
---    `clock_gettime(CLOCK_PROCESS_CPUTIME_ID)`.
---
---    Return the CPU time consumed by the process, in nanoseconds.
---
---    **Example:**
---
---    ```lua
---    -- This will print CPU nanoseconds consumed by the process.
---    clock = require('clock')
---    print(clock.proc64())
---    ```
---@field thread fun(): number Get the thread time in seconds.
---
---    Derived from C function
---    `clock_gettime(CLOCK_THREAD_CPUTIME_ID)`.
---
---    This is the best function to use with benchmarks that need to
---    calculate how much time has been spent within a thread within a
---    CPU.
---
---    Return the CPU time consumed by the transaction processor
---    thread, in seconds.
---
---    **Example:**
---
---    ```lua
---    -- This will print CPU seconds consumed by the transaction
---    -- processor thread.
---    clock = require('clock')
---    print(clock.thread())
---    ```
---@field thread64 fun(): ffi.cdata* Get the thread time in nanoseconds.
---
---    Derived from C function
---    `clock_gettime(CLOCK_THREAD_CPUTIME_ID)`.
---
---    Return the CPU time consumed by the transaction processor
---    thread, in nanoseconds.
---
---    **Example:**
---
---    ```lua
---    -- This will print CPU nanoseconds consumed by the transaction
---    -- processor thread.
---    clock = require('clock')
---    print(clock.thread64())
---    ```
---@field bench fun(
---    func: function,
---    ...: any
---): table Measure the time a function takes within a processor.
---
---    This function uses `clock.proc()`, therefore it calculates elapsed
---    CPU time. It is not useful for showing actual elapsed time.
---
---    Parameters:
---
---    * `func` -- function or function reference.
---    * `...` -- whatever values are required by the function.
---
---    Returns a table. The first element is seconds of CPU time, the second
---    is whatever the function returns.
---
---    **Example:**
---
---    ```lua
---    -- Benchmark a function which sleeps 10 seconds.
---    -- NB: bench() will not calculate sleep time.
---    -- So the returned value will be {a number less than 10, 88}.
---    clock = require('clock')
---    fiber = require('fiber')
---    function f(param)
---      fiber.sleep(param)
---      return 88
---    end
---    clock.bench(f, 10)
---    ```

---@type clock
---@diagnostic disable-next-line: missing-fields
local clock = {}
local ffi = require('ffi')

ffi.cdef[[
    double clock_realtime(void);
    double clock_monotonic(void);
    double clock_process(void);
    double clock_thread(void);
    int64_t clock_realtime64(void);
    int64_t clock_monotonic64(void);
    int64_t clock_process64(void);
    int64_t clock_thread64(void);
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

---Measure the time a function takes within a processor.
---
---@param fun function callback to measure.
---@param ... any whatever values are required by the function.
---@return table result seconds of CPU time and the function result.
clock.bench = function(fun, ...)
    local overhead = clock.proc()
    overhead = clock.proc() - overhead
    local start_time = clock.proc()
    local res = {0, fun(...)}
    res[1] = clock.proc() - start_time - overhead
    return res
end

---@type clock
return clock
