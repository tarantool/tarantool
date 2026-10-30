local fiber = require('fiber')
local clock = require('clock')
local ffi = require('ffi')
local fio = require('fio')
local errno = require('errno')

-- For process_is_alive.
ffi.cdef([[
    int kill(pid_t pid, int sig);
]])

--- Verify whether a process is alive.
local function process_is_alive(pid)
    local rc = ffi.C.kill(pid, 0)
    return rc == 0 or errno() ~= errno.ESRCH
end

--- Returns true if process completed before timeout, false otherwise.
local function wait_process_completion(pid, timeout)
    local start_time = clock.monotonic()
    local process_completed = false
    while clock.monotonic() - start_time < timeout do
        if not process_is_alive(pid) then
            process_completed = true
            break
        end
        fiber.sleep(0.01)
    end
    return process_completed
end

--- Open file on reading with timeout.
local function open_with_timeout(filename, timeout)
    local fh
    local start_time = clock.monotonic()
    while not fh and clock.monotonic() - start_time < timeout do
        fh = fio.open(filename, {'O_RDONLY'})
        fiber.sleep(0.01)
    end
    return fh
end

--- Try to read from file with timeout with interval.
local function read_with_timeout(fh, timeout, interval)
    local data = ''
    local start_time = clock.monotonic()
    while #data == 0 and clock.monotonic() - start_time < timeout do
        data = fh:read()
        if #data == 0 then fiber.sleep(interval) end
    end
    return data
end

return {
    process_is_alive = process_is_alive,
    wait_process_completion = wait_process_completion,
    open_with_timeout = open_with_timeout,
    read_with_timeout = read_with_timeout
}
