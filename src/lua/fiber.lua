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
]]
local C = ffi.C

local function fiber_time()
    return tonumber(C.fiber_time())
end

local function fiber_time64()
    return C.fiber_time64()
end

local function fiber_clock()
    return tonumber(C.fiber_clock())
end

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

-- Start from '_' to hide it from auto completion.
fiber._internal = fiber._internal or {}
fiber._internal.schedule_task = worker_schedule_task
fiber._internal.set_system = fiber_set_system
fiber._internal.set_managed_shutdown = fiber_set_managed_shutdown

setmetatable(fiber, {__serialize = function(self)
    local res = table.copy(self)
    res._internal = nil
    return setmetatable(res, {})
end})

return fiber
