-- fiber.lua (internal file)

local fiber = require('fiber')
local ffi = require('ffi')
ffi.cdef[[
double
fiber_time(void);
int64_t
fiber_time64(void);
double
fiber_clock(void);
int64_t
fiber_clock64(void);
bool
cord_is_main(void);
]]
local C = ffi.C

---@alias fiber.status "running" | "dead" | "suspended"
---@alias fiber.slice_arg fiber.slice | number
---@alias fiber.info_opts { backtrace?: boolean, bt?: boolean }

---@class fiber
---Create and start a fiber.
---
---The fiber is created and begins to run immediately.
---
---**Example:**
---
---The script below shows how to create a fiber using `fiber.create`.
---
--- ```lua
--- -- app.lua --
--- fiber = require('fiber')
---
--- function greet(name)
--- print('Hello, '..name)
--- end
---
--- greet_fiber = fiber.create(greet, 'John')
--- print('Fiber already started')
--- ```
---
---@field create fun<T>(func: fun(...: T...), ...: T...): fiber_object
---Create a fiber but do not start it.
---
---The created fiber starts after the fiber creator (that is, the job that
---is calling `fiber.new()`) yields. The initial fiber state is ready.
---
---**Note:** `fiber.status()` returns the suspended state for ready fibers
---because the ready state is not observable using the fiber module API.
---
---You can join fibers created using `fiber.new()` by calling the
---`fiber_object:join()` function and get the result returned by the fiber's
---function.
---
---To join the fiber, you need to make it joinable using
---`fiber_object:set_joinable()`.
---
---@field new fun<T>(func: fun(...: T...), ...: T...): fiber_object
---Get the currently scheduled fiber.
---
---@field self fun(): fiber_object
---Get a fiber object by ID.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.find(101)
--- ---
--- - status: running
--- name: interactive
--- id: 101
--- ...
--- ```
---
---@field find fun(id: integer): fiber_object
---Yield control to the scheduler and sleep for the specified number of
---seconds.
---
---Only the current fiber can be made to sleep.
---
---**Example:**
---
---The `increment` function below contains an infinite loop that adds 1 to
---the `counter` global variable. Then, the current fiber goes to sleep for
---`period` seconds. `sleep` causes an implicit `fiber.yield()`.
---
--- ```lua
--- -- app.lua --
--- fiber = require('fiber')
---
--- counter = 0
--- function increment(period)
---     while true do
---         counter = counter + 1
---         fiber.sleep(period)
---     end
--- end
---
--- increment_fiber = fiber.create(increment, 2)
--- require('console').start()
--- ```
---
---@field sleep fun(timeout: number)
---Yield control to the scheduler.
---
---Equivalent to `fiber.sleep(0)`.
---
---**Example:**
---
---In the example below, two fibers are associated with the same function.
---Each fiber yields control after printing a greeting.
---
--- ```lua
--- -- app.lua --
--- fiber = require('fiber')
---
--- function greet()
---     while true do
---         print('Enter a name:')
---         name = io.read()
---         print('Hello, '..name..'. I am fiber '..fiber.id())
---         fiber.yield()
---     end
--- end
---
--- for i = 1, 2 do
---     fiber_object = fiber.create(greet)
---     fiber_object:cancel()
--- end
--- ```
---
---@field yield fun()
---Return the status of the current fiber.
---
---Or, if optional fiber_object is passed, return the status of the
---specified fiber.
---
--- ```tarantoolsession
--- tarantool> fiber.status()
--- ---
--- - running
--- ...
--- ```
---
---@field status fun(fiber_object?: fiber_object): fiber.status
---Return information about all fibers.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.info({ bt = true })
--- ---
--- - 101:
--- csw: 1
--- backtrace:
--- - C: '#0  0x5dd130 in lbox_fiber_id+96'
--- - C: '#1  0x5dd13d in lbox_fiber_stall+13'
--- - L: stall in =[C] at line -1
--- - L: (unnamed) in @builtin/fiber.lua at line 59
--- - C: '#2  0x66371b in lj_BC_FUNCC+52'
--- - C: '#3  0x628f28 in lua_pcall+120'
--- - C: '#4  0x5e22a8 in luaT_call+24'
--- - C: '#5  0x5dd1a9 in lua_fiber_run_f+89'
--- - C: '#6  0x45b011 in fiber_cxx_invoke(
---   int (*)(__va_list_tag*), __va_list_tag*)+17'
--- - C: '#7  0x5ff3c0 in fiber_loop+48'
--- - C: '#8  0x81ecf4 in coro_init+68'
--- memory:
--- total: 516472
--- used: 0
--- time: 0
--- name: lua
--- fid: 101
--- 102:
--- csw: 0
--- backtrace:
--- - C: '#0  (nil) in +63'
--- - C: '#1  (nil) in +63'
--- memory:
--- total: 516472
--- used: 0
--- time: 0
--- name: on_shutdown
--- fid: 102
---
--- ...
--- ```
---
---@field info fun(opts?: fiber.info_opts): table<integer, fiber.info>
---@field id fun(): integer
---Show all alive fibers and their CPU consumption.
---
---**Note:** *Since 2.11.0* `cpu_misses` is deprecated and always returns
---`0`.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.top()
--- ---
--- - cpu:
--- 107/lua:
--- instant: 30.967324490456
--- time: 0.351821993
--- average: 25.582738345233
--- 104/lua:
--- instant: 9.6473633128437
--- time: 0.110869897
--- average: 7.9693406131877
--- 101/on_shutdown:
--- instant: 0
--- time: 0
--- average: 0
--- 103/lua:
--- instant: 9.8026528631511
--- time: 0.112641118
--- average: 18.138387232255
--- 106/lua:
--- instant: 20.071174377224
--- time: 0.226901357
--- average: 17.077908441831
--- 102/interactive:
--- instant: 0
--- time: 9.6858e-05
--- average: 0
--- 105/lua:
--- instant: 9.2461986412164
--- time: 0.10657528
--- average: 7.7068458630827
--- 1/sched:
--- instant: 20.265286315108
--- time: 0.237095335
--- average: 23.141537169257
--- cpu_misses: 0
--- ...
--- ```
---
---@field top fun(): { cpu: table<string, fiber.top>, cpu_misses: number }
---Cancel a fiber.
---
---Locate a fiber by its numeric ID and cancel it. In other words,
---`fiber.kill()` combines `fiber.find()` and `fiber_object:cancel()`.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.kill(fiber.id()) -- kill self, may make program end
--- ---
--- - error: fiber is cancelled
--- ...
--- ```
---
---@field kill fun(fiber_object: fiber_object)
---Check if the current fiber has been cancelled and throw an exception if
---this is the case.
---
---**Note:**
---
---Even if you catch the exception, the fiber will remain cancelled. Most
---types of calls will check `fiber.testcancel()`. However, some functions
---(`id`, `status`, `join` etc.) will return no error.
---
---We recommend application developers to implement occasional checks with
---`fiber.testcancel()` and to end fiber's execution as soon as possible in
---case it has been cancelled.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.testcancel()
--- ---
--- - error: fiber is cancelled
--- ...
--- ```
---
---@field testcancel fun()
---Get the current system time (in seconds since the epoch) as a Lua number.
---
---@field time fun(): number
---Get the current system time (in microseconds since the epoch) as a 64-bit
---integer.
---
---@field time64 fun(): ffi.cdata*
---Get the monotonic time in seconds.
---
---@field clock fun(): number
---Get the monotonic time in microseconds.
---
---@field clock64 fun(): ffi.cdata*
---Enable `fiber.top`.
---
---**Note:** Enabling `fiber.top()` slows down fiber switching by about 15%,
---so it is disabled by default.
---
---@see fiber.top
---
---@field top_enable fun()
---Disable `fiber.top`.
---
---**Note:** Enabling `fiber.top()` slows down fiber switching by about 15%,
---so it is disabled by default.
---
---@see fiber.top
---
---@field top_disable fun()
---Check whether a slice for the current fiber is over.
---
---A fiber slice limits the time period of executing a fiber without
---yielding control.
---
---**Example:**
---
---The example below shows how to use `set_max_slice` to limit the slice for
---all fibers. `fiber.check_slice()` is called inside a long-running
---operation to determine whether a slice for the current fiber is over.
---
--- ```lua
--- -- app.lua --
--- fiber = require('fiber')
--- clock = require('clock')
---
--- fiber.set_max_slice({warn = 1.5, err = 3})
--- time = clock.monotonic()
--- function long_operation()
---     while clock.monotonic() - time < 5 do
---         fiber.check_slice()
---         -- Long-running operation --
---     end
--- end
---
--- long_operation_fiber = fiber.create(long_operation)
--- ```
---
---The output should look as follows:
---
--- ```console
--- $ tarantool app.lua
--- fiber has not yielded for more than 1.500 seconds
--- FiberSliceIsExceeded: fiber slice is exceeded
--- ```
---
---@field check_slice fun()
---Set a slice for the current fiber execution.
---
---A fiber slice limits the time period of executing a fiber without
---yielding control.
---
---@field set_slice fun(slice: fiber.slice_arg) a time period (in
---    seconds) that specifies the error slice
---Set the default maximum slice for all fibers.
---
---A fiber slice limits the time period of executing a fiber without
---yielding control.
---
---@field set_max_slice fun(slice: fiber.slice_arg) a time period (in
---    seconds) that specifies the error slice
---Extend a slice for the current fiber execution.
---
---For example, if the default error slice is set using
---`fiber.set_max_slice()` to 3 seconds, `extend_slice(1)` extends the error
---slice to 4 seconds.
---
---@field extend_slice fun(slice: fiber.slice_arg) a time period (in
---    seconds) that specifies the error slice
---@field channel fun(capacity?: number): fiber.channel Create a channel.
---@field cond fun(): fiber.cond Create a conditional variable.

---@class fiber.slice
---@field warn number A warning slice in seconds.
---@field err number An error slice in seconds.

---@class fiber.info
---@field csw number Context switch count.
---@field memory { total: number, used: number } `total` is memory occupied
---    by the fiber as a C structure, its stack, etc. `used` is memory
---    used by the fiber.
---@field time number duplicates the "time" entry from fiber.top().cpu for
---    each fiber. (Only shown if fiber.top is enabled.)
---@field name string name of the fiber
---@field fid number id of the fiber
---@field backtrace { C: string, L: string }[] fiber's stack trace

---@class fiber.top
---@field instant number (in percent) a number which indicates the share of
---    time the fiber was executing during the previous event loop iteration
---@field average number (in percent) a number which is calculated as an
---    exponential moving average of instant values over all the previous
---    event loop iterations
---@field time number (in seconds) a number which estimates how much CPU
---    time each fiber spent processing during its lifetime

---@alias fiber.name_options {truncate: boolean}

---@class fiber_object: userdata
---Get a fiber's ID.
---
---@field id fun(self: fiber_object): number # fiber id
---Get or change a fiber's name.
---
---Change the fiber name. By default a Tarantool server's interactive-mode
---fiber is named 'interactive' and new fibers created due to `fiber.create`
---are named 'lua'.
---
---Giving fibers distinct names makes it easier to distinguish them when
---using `fiber.info` and `fiber.top()`.
---
---Max length is 255.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.self():name('non-interactive')
--- ---
--- ...
--- ```
---
---@field name fun(self: fiber_object, name?: string,
---    options?: fiber.name_options): string Get or set a name.
---Get a fiber's status.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.self():status()
--- ---
--- - running
--- ...
--- ```
---
---@field status fun(self: fiber_object): fiber.status Get a fiber's status.
---Send a cancellation request to the fiber.
---
---Running and suspended fibers can be cancelled. After a fiber has been
---cancelled, attempts to operate on it cause errors, for example,
---`fiber_object:name()` causes `error: the fiber is dead`. But a dead fiber
---can still report its ID and status.
---
---Cancellation is asynchronous.
---
---Use `fiber_object:join()` to wait for the cancellation to complete.
---
---After `fiber_object:cancel()` is called, the fiber may or may not check
---whether it was cancelled. If the fiber does not check it, it cannot ever
---be cancelled.
---
---**Possible errors:** cancel is not permitted for the specified fiber
---object.
---
---@field cancel fun() Send a cancellation request.
---@field set_max_slice fun(self: fiber_object, slice: fiber.slice_arg) Set max.
---Local storage within the fiber.
---@field storage table Local storage within the fiber.
---Make a fiber joinable.
---
---A joinable fiber can be waited for using `fiber_object:join()`.
---
---The best practice is to call `fiber_object:set_joinable()` before the
---fiber function begins to execute because otherwise the fiber could become
---`dead` before `fiber_object:set_joinable()` takes effect.
---
---The usual sequence could be:
---1. Call `fiber.new()` instead of `fiber.create()` to create a new
---   fiber_object. Do not yield at this point, because that will cause the
---   fiber function to begin.
---2. Call `fiber_object:set_joinable(true)` to make the new `fiber_object`
---   joinable. Now it is safe to yield.
---3. Call `fiber_object:join()`.
---
---Usually `fiber_object:join()` should be called, otherwise the fiber's
---status may become 'suspended' when the fiber function ends, instead of
---'dead'.
---
---@field set_joinable fun(self: fiber_object, joinable: boolean) Make joinable.
---"Join" a joinable fiber.
---
---That is, let the fiber's function run and wait until the fiber's status
---is 'dead' (normally a status becomes 'dead' when the function execution
---finishes).
---
---Joining will cause a yield, therefore, if the fiber is currently in a
---suspended state, execution of its fiber function will resume.
---
---This kind of waiting is more convenient than going into a loop and
---periodically checking the status.
---
---However, it works only if the fiber was created with `fiber.new()` and
---was made joinable with `fiber_object:set_joinable()`.
---
---@field join fun(self: fiber_object): boolean Join a fiber.

---@class fiber.channel
---Send a message using a channel.
---
---If the channel is full, `channel:put()` waits until there is a free slot
---in the channel.
---
---**Note:**
---
---The default `channel capacity` is `0`.
---
---With this default value, `channel:put()` *waits infinitely* until
---`channel:get()` is called.
---
---If `timeout` is specified, and there is no free slot in the channel for
---the duration of the timeout, then the return value is false. If the
---channel is closed, then the return value is false. Otherwise, the return
---value is true, indicating success.
---
---@field put fun(message: any, timeout?: number): boolean Send a message.
---Close the channel.
---
---All waiters in the channel will stop waiting.
---
---All following `channel:get()` operations will return `nil`, and all
---following `channel:put()` operations will return `false`.
---
---@field close fun() Close the channel.
---Fetch and remove a message from a channel.
---
---If the channel is empty, `channel:get()` waits for a message.
---
---@field get fun(timeout?: number): any Fetch and remove a message.
---Check whether the channel is empty (has no messages).
---
---@field is_empty fun(): boolean Check whether the channel is empty.
---Find out how many messages are in the channel.
---
---@field count fun(): integer Find out how many messages are in the channel.
---Returns size of channel.
---
---@field size fun(self: fiber.channel): integer
---Check if a channel is full.
---
---@field is_full fun(): boolean Check if a channel is full.
---Check whether readers are waiting for a message because they have issued
---`channel:get()` and the channel is empty.
---
---@field has_readers fun(self: fiber.channel): boolean
---Check whether writers are waiting because they have issued
---`channel:put()` and the channel is full.
---
---@field has_writers fun(self: fiber.channel): boolean
---Check if a channel is closed.
---
---@field is_closed fun(): boolean Check if a channel is closed.

---@class fiber.cond: userdata
---Make the current fiber go to sleep.
---
---Waiting until another fiber invokes the `signal()` or `broadcast()` method
---on the cond object.
---
---The sleep causes an implicit `fiber.yield()`.
---
---If `timeout` is provided, and a signal doesn't happen for the duration of
---the timeout, `wait()` returns false. If a signal or broadcast happens,
---`wait()` returns true.
---
---@field wait fun(timeout?: number): boolean Wait for a signal.
---Wake up a single fiber that has executed `wait()` for the same variable.
---
---Does not yield.
---
---@field signal fun() Wake up a single fiber.
---Wake up all fibers that have executed `wait()` for the same variable.
---
---Does not yield.
---
---@field broadcast fun() Wake up all fibers.

---Get the current system time (in seconds since the epoch) as a Lua number.
---
---The time is taken from the event loop clock, which makes this call very
---cheap, but still useful for constructing artificial tuple keys.
---
---**Example:**
---
--- ```
--- tarantool> fiber.time(), fiber.time()
--- - 1448466279.2415
--- - 1448466279.2415
--- ```
---
---@see fiber.time64
---
---@return number
local function fiber_time()
    ---@diagnostic disable-next-line: return-type-mismatch
    return tonumber(C.fiber_time())
end

---Get the current system time (in microseconds since the epoch) as a 64-bit
---integer.
---
---The time is taken from the event loop clock.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber.time(), fiber.time64()
--- ---
--- - 1448466351.2708
--- - 1448466351270762
--- ...
--- ```
---
---@see fiber.time
---
---@return ffi.cdata*
local function fiber_time64()
    return C.fiber_time64()
end

---Get the monotonic time in seconds.
---
---It is better to use `fiber.clock()` for calculating timeouts instead of
---`fiber.time()` because `fiber.time()` reports real time so it is affected
---by system time changes.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> start = fiber.clock()
--- ---
--- ...
--- tarantool> print(start)
--- 248700.58805
--- ---
--- ...
--- tarantool> print(fiber.time(), fiber.time()-start)
--- 1600785979.8291 1600537279.241
--- ---
--- ...
--- ```
---
---@see fiber.clock64
---
---@return number
local function fiber_clock()
    ---@diagnostic disable-next-line: return-type-mismatch
    return tonumber(C.fiber_clock())
end

---Get the monotonic time in microseconds.
---
---Same as `fiber.clock()` but in microseconds.
---
---@see fiber.clock
---
---@return ffi.cdata*
local function fiber_clock64()
    return C.fiber_clock64()
end

fiber.time = fiber_time
fiber.time64 = fiber_time64
fiber.clock = fiber_clock
fiber.clock64 = fiber_clock64

local stall = fiber.stall
local fiber_set_system = fiber.set_system
local fiber_set_managed_shutdown = fiber.set_managed_shutdown
fiber.stall = nil
fiber.set_system = nil
fiber.set_managed_shutdown = nil

local worker_next_task = nil
local worker_last_task
local worker_fiber

--
-- Worker is a singleton fiber for not urgent delayed execution of
-- functions. Main purpose - schedule execution of a function,
-- which is going to yield, from a context, where a yield is not
-- allowed. Such as an FFI object's GC callback.
--
local function worker_f()
    local task
    while true do
        while true do
            task = worker_next_task
            if task then
                break
            end
            stall()
        end
        worker_next_task = task.next
        pcall(task.f, task.arg)
        fiber.sleep(0)
    end
end

local worker_name = 'tasks_worker_fiber'

worker_fiber = fiber.new(worker_f)
fiber_set_system(worker_fiber)
fiber_set_managed_shutdown(worker_fiber)
worker_fiber:name(worker_name)

local function worker_schedule_task(f, arg)
    local task = {f = f, arg = arg}
    if not worker_next_task then
        worker_next_task = task
    else
        worker_last_task.next = task
    end
    worker_last_task = task
    -- Fiber is finished on shutdown as it has managed shutdown.
    if worker_fiber:status() ~= 'dead' then
        worker_fiber:wakeup()
    end
end

---@type fun<T>(func: fun(...: T...), ...: T...): fiber_object
local create = {} -- luacheck: ignore 311
create = fiber.create

---@type fun<T>(func: fun(...: T...), ...: T...): fiber_object
local new = {} -- luacheck: ignore 311
new = fiber.new

---@type fun(): fiber_object
local self = {} -- luacheck: ignore 311
self = fiber.self

---@type fun(id: integer): fiber_object
local find = {} -- luacheck: ignore 311
find = fiber.find

---@type fun(timeout: number)
local sleep = {} -- luacheck: ignore 311
sleep = fiber.sleep

---@type fun()
local yield = {} -- luacheck: ignore 311
yield = fiber.yield

---@type fun(fiber_object?: fiber_object): fiber.status
local status = {} -- luacheck: ignore 311
status = fiber.status

---@type fun(opts?: fiber.info_opts): table<integer, fiber.info>
local info = {} -- luacheck: ignore 311
info = fiber.info

---@type fun(): integer
local id = {} -- luacheck: ignore 311
id = fiber.id

---@type fun(): { cpu: table<string, fiber.top>, cpu_misses: number }
local top = {} -- luacheck: ignore 311
top = fiber.top

---@type fun(fiber_object: fiber_object)
local kill = {} -- luacheck: ignore 311
kill = fiber.kill

---@type fun()
local testcancel = {} -- luacheck: ignore 311
testcancel = fiber.testcancel

---@type fun(): number
local time = {} -- luacheck: ignore 311
time = fiber.time

---@type fun(): ffi.cdata*
local time64 = {} -- luacheck: ignore 311
time64 = fiber.time64

---@type fun(): number
local clock = {} -- luacheck: ignore 311
clock = fiber.clock

---@type fun(): ffi.cdata*
local clock64 = {} -- luacheck: ignore 311
clock64 = fiber.clock64

---@type fun()
local top_enable = {} -- luacheck: ignore 311
top_enable = fiber.top_enable

---@type fun()
local top_disable = {} -- luacheck: ignore 311
top_disable = fiber.top_disable

---@type fun()
local check_slice = {} -- luacheck: ignore 311
check_slice = fiber.check_slice

---@type fun(slice: fiber.slice_arg)
local set_slice = {} -- luacheck: ignore 311
set_slice = fiber.set_slice

---@type fun(slice: fiber.slice_arg)
local set_max_slice = {} -- luacheck: ignore 311
set_max_slice = fiber.set_max_slice

---@type fun(slice: fiber.slice_arg)
local extend_slice = {} -- luacheck: ignore 311
extend_slice = fiber.extend_slice

---@type fun(capacity?: number): fiber.channel
local channel = {} -- luacheck: ignore 311
channel = fiber.channel

---@type fun(): fiber.cond
local cond = {} -- luacheck: ignore 311
cond = fiber.cond

---With the fiber module, you can:
---
---* Create, run, and manage fibers.
---* Send and receive messages between different processes (i.e. different
---  connections, sessions, or fibers) via channels.
---* Use a synchronization mechanism for fibers, similar to "condition
---  variables" and similar to operating-system functions, such as
---  `pthread_cond_wait()` plus `pthread_cond_signal()`.
---
---**Fibers**
---
---A fiber is a set of instructions executed with cooperative multitasking.
---A fiber can be running, suspended, ready or dead. At any moment only one
---fiber is running; it yields control to another ready fiber explicitly.
---When the fiber function ends, the fiber becomes dead. A running or
---suspended fiber can be cancelled.
---
---**Channels**
---
---Call `fiber.channel()` to create a channel. Channels are used to send,
---receive and check messages between fibers. Message exchange is
---synchronous. Use object-oriented syntax, for example
---`channel:put(message)`.
---
---**Condition variables**
---
---Call `fiber.cond()` to create a condition variable. `cond:wait()` makes
---the current fiber sleep until another fiber calls `cond:signal()` or
---`cond:broadcast()`.

local m = { -- luacheck: ignore 211
    ---Create and start a fiber.
    ---
    ---The fiber is created and begins to run immediately.
    ---
    ---**Example:**
    ---
    ---The script below shows how to create a fiber using `fiber.create`.
    ---
    --- ```lua
    --- -- app.lua --
    --- fiber = require('fiber')
    ---
    --- function greet(name)
    --- print('Hello, '..name)
    --- end
    ---
    --- greet_fiber = fiber.create(greet, 'John')
    --- print('Fiber already started')
    --- ```
    ---
    create = create,
    ---Create a fiber but do not start it.
    ---
    ---The created fiber starts after the fiber creator (that is, the job that
    ---is calling `fiber.new()`) yields. The initial fiber state is ready.
    ---
    ---**Note:** `fiber.status()` returns the suspended state for ready fibers
    ---because the ready state is not observable using the fiber module API.
    ---
    ---You can join fibers created using `fiber.new()` by calling the
    ---`fiber_object:join()` function and get the result returned by the fiber's
    ---function.
    ---
    ---To join the fiber, you need to make it joinable using
    ---`fiber_object:set_joinable()`.
    ---
    new = new,
    ---Get the currently scheduled fiber.
    ---
    self = self,
    ---Get a fiber object by ID.
    ---
    ---**Example:**
    ---
    --- ```tarantoolsession
    --- tarantool> fiber.find(101)
    --- ---
    --- - status: running
    --- name: interactive
    --- id: 101
    --- ...
    --- ```
    ---
    find = find,
    ---Yield control to the scheduler and sleep for the specified number of
    ---seconds.
    ---
    ---Only the current fiber can be made to sleep.
    ---
    ---**Example:**
    ---
    ---The `increment` function below contains an infinite loop that adds 1 to
    ---the `counter` global variable. Then, the current fiber goes to sleep for
    ---`period` seconds. `sleep` causes an implicit `fiber.yield()`.
    ---
    --- ```lua
    --- -- app.lua --
    --- fiber = require('fiber')
    ---
    --- counter = 0
    --- function increment(period)
    ---     while true do
    ---         counter = counter + 1
    ---         fiber.sleep(period)
    ---     end
    --- end
    ---
    --- increment_fiber = fiber.create(increment, 2)
    --- require('console').start()
    --- ```
    ---
    sleep = sleep,
    ---Yield control to the scheduler.
    ---
    ---Equivalent to `fiber.sleep(0)`.
    ---
    ---**Example:**
    ---
    ---In the example below, two fibers are associated with the same function.
    ---Each fiber yields control after printing a greeting.
    ---
    --- ```lua
    --- -- app.lua --
    --- fiber = require('fiber')
    ---
    --- function greet()
    ---     while true do
    ---         print('Enter a name:')
    ---         name = io.read()
    ---         print('Hello, '..name..'. I am fiber '..fiber.id())
    ---         fiber.yield()
    ---     end
    --- end
    ---
    --- for i = 1, 2 do
    ---     fiber_object = fiber.create(greet)
    ---     fiber_object:cancel()
    --- end
    --- ```
    ---
    yield = yield,
    ---Return the status of the current fiber.
    ---
    ---Or, if optional fiber_object is passed, return the status of the
    ---specified fiber.
    ---
    --- ```tarantoolsession
    --- tarantool> fiber.status()
    --- ---
    --- - running
    --- ...
    --- ```
    ---
    status = status,
    ---Return information about all fibers.
    ---
    ---**Example:**
    ---
    --- ```tarantoolsession
    --- tarantool> fiber.info({ bt = true })
    --- ---
    --- - 101:
    --- csw: 1
    --- backtrace:
    --- - C: '#0  0x5dd130 in lbox_fiber_id+96'
    --- - C: '#1  0x5dd13d in lbox_fiber_stall+13'
    --- - L: stall in =[C] at line -1
    --- - L: (unnamed) in @builtin/fiber.lua at line 59
    --- - C: '#2  0x66371b in lj_BC_FUNCC+52'
    --- - C: '#3  0x628f28 in lua_pcall+120'
    --- - C: '#4  0x5e22a8 in luaT_call+24'
    --- - C: '#5  0x5dd1a9 in lua_fiber_run_f+89'
    --- - C: '#6  0x45b011 in fiber_cxx_invoke(
    ---   int (*)(__va_list_tag*), __va_list_tag*)+17'
    --- - C: '#7  0x5ff3c0 in fiber_loop+48'
    --- - C: '#8  0x81ecf4 in coro_init+68'
    --- memory:
    --- total: 516472
    --- used: 0
    --- time: 0
    --- name: lua
    --- fid: 101
    --- 102:
    --- csw: 0
    --- backtrace:
    --- - C: '#0  (nil) in +63'
    --- - C: '#1  (nil) in +63'
    --- memory:
    --- total: 516472
    --- used: 0
    --- time: 0
    --- name: on_shutdown
    --- fid: 102
    ---
    --- ...
    --- ```
    ---
    info = info,
    id = id,
    ---Show all alive fibers and their CPU consumption.
    ---
    ---**Note:** *Since 2.11.0* `cpu_misses` is deprecated and always returns
    ---`0`.
    ---
    ---**Example:**
    ---
    --- ```tarantoolsession
    --- tarantool> fiber.top()
    --- ---
    --- - cpu:
    --- 107/lua:
    --- instant: 30.967324490456
    --- time: 0.351821993
    --- average: 25.582738345233
    --- 104/lua:
    --- instant: 9.6473633128437
    --- time: 0.110869897
    --- average: 7.9693406131877
    --- 101/on_shutdown:
    --- instant: 0
    --- time: 0
    --- average: 0
    --- 103/lua:
    --- instant: 9.8026528631511
    --- time: 0.112641118
    --- average: 18.138387232255
    --- 106/lua:
    --- instant: 20.071174377224
    --- time: 0.226901357
    --- average: 17.077908441831
    --- 102/interactive:
    --- instant: 0
    --- time: 9.6858e-05
    --- average: 0
    --- 105/lua:
    --- instant: 9.2461986412164
    --- time: 0.10657528
    --- average: 7.7068458630827
    --- 1/sched:
    --- instant: 20.265286315108
    --- time: 0.237095335
    --- average: 23.141537169257
    --- cpu_misses: 0
    --- ...
    --- ```
    ---
    top = top,
    ---Cancel a fiber.
    ---
    ---Locate a fiber by its numeric ID and cancel it. In other words,
    ---`fiber.kill()` combines `fiber.find()` and `fiber_object:cancel()`.
    ---
    ---**Example:**
    ---
    --- ```tarantoolsession
    --- tarantool> fiber.kill(fiber.id()) -- kill self, may make program end
    --- ---
    --- - error: fiber is cancelled
    --- ...
    --- ```
    ---
    kill = kill,
    ---Check if the current fiber has been cancelled and throw an exception if
    ---this is the case.
    ---
    ---**Note:**
    ---
    ---Even if you catch the exception, the fiber will remain cancelled. Most
    ---types of calls will check `fiber.testcancel()`. However, some functions
    ---(`id`, `status`, `join` etc.) will return no error.
    ---
    ---We recommend application developers to implement occasional checks with
    ---`fiber.testcancel()` and to end fiber's execution as soon as possible in
    ---case it has been cancelled.
    ---
    ---**Example:**
    ---
    --- ```tarantoolsession
    --- tarantool> fiber.testcancel()
    --- ---
    --- - error: fiber is cancelled
    --- ...
    --- ```
    ---
    testcancel = testcancel,
    ---Get the current system time (in seconds since the epoch) as a Lua number.
    ---
    time = time,
    ---Get the current system time (in microseconds since the epoch) as a 64-bit
    ---integer.
    ---
    time64 = time64,
    ---Get the monotonic time in seconds.
    ---
    clock = clock,
    ---Get the monotonic time in microseconds.
    ---
    clock64 = clock64,
    ---Enable `fiber.top`.
    ---
    ---**Note:** Enabling `fiber.top()` slows down fiber switching by about 15%,
    ---so it is disabled by default.
    ---
    ---@see fiber.top
    ---
    top_enable = top_enable,
    ---Disable `fiber.top`.
    ---
    ---**Note:** Enabling `fiber.top()` slows down fiber switching by about 15%,
    ---so it is disabled by default.
    ---
    ---@see fiber.top
    ---
    top_disable = top_disable,
    ---Check whether a slice for the current fiber is over.
    ---
    ---A fiber slice limits the time period of executing a fiber without
    ---yielding control.
    ---
    ---**Example:**
    ---
    ---The example below shows how to use `set_max_slice` to limit the slice for
    ---all fibers. `fiber.check_slice()` is called inside a long-running
    ---operation to determine whether a slice for the current fiber is over.
    ---
    --- ```lua
    --- -- app.lua --
    --- fiber = require('fiber')
    --- clock = require('clock')
    ---
    --- fiber.set_max_slice({warn = 1.5, err = 3})
    --- time = clock.monotonic()
    --- function long_operation()
    ---     while clock.monotonic() - time < 5 do
    ---         fiber.check_slice()
    ---         -- Long-running operation --
    ---     end
    --- end
    ---
    --- long_operation_fiber = fiber.create(long_operation)
    --- ```
    ---
    ---The output should look as follows:
    ---
    --- ```console
    --- $ tarantool app.lua
    --- fiber has not yielded for more than 1.500 seconds
    --- FiberSliceIsExceeded: fiber slice is exceeded
    --- ```
    ---
    check_slice = check_slice,
    ---Set a slice for the current fiber execution.
    ---
    ---A fiber slice limits the time period of executing a fiber without
    ---yielding control.
    ---
    set_slice = set_slice,
    ---    seconds) that specifies the error slice
    ---Set the default maximum slice for all fibers.
    ---
    ---A fiber slice limits the time period of executing a fiber without
    ---yielding control.
    ---
    set_max_slice = set_max_slice,
    ---    seconds) that specifies the error slice
    ---Extend a slice for the current fiber execution.
    ---
    ---For example, if the default error slice is set using
    ---`fiber.set_max_slice()` to 3 seconds, `extend_slice(1)` extends the error
    ---slice to 4 seconds.
    ---
    extend_slice = extend_slice,
    ---    seconds) that specifies the error slice
    channel = channel,
    cond = cond,
}

-- Start from '_' to hide it from auto completion.
fiber._internal = fiber._internal or {}
fiber._internal.schedule_task = worker_schedule_task
fiber._internal.set_system = fiber_set_system
fiber._internal.set_managed_shutdown = fiber_set_managed_shutdown
fiber._internal.cord_is_main = C.cord_is_main()

setmetatable(fiber, {__serialize = function(self)
    local res = table.copy(self)
    res._internal = nil
    return setmetatable(res, {})
end})

local fiber_api = fiber
---@cast fiber_api fiber
return fiber_api
