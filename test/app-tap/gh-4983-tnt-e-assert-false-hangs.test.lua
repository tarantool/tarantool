#!/usr/bin/env tarantool

-- Using io.popen and self written module process_timeout
-- in presence of the 'popen' built-in module because the
-- last one isn't available in 1.10
local process_timeout = require('process_timeout')
local tap = require('tap')
local fio = require('fio')
local ffi = require('ffi')

--
-- gh-4983: tarantool -e 'assert(false)' hangs
--

local TARANTOOL_PATH = arg[-1]
local output_file = fio.abspath('gh-4983_out.txt')
local line = ('%s -e "assert(false)" > %s 2>&1 & echo $!'):
        format(TARANTOOL_PATH, output_file)

-- Like a default timeout for `cond_wait` in test-run
local process_waiting_timeout = 60.0
local file_read_timeout = 60.0
local file_open_timeout = 60.0
local file_read_interval = 0.2

local res = tap.test('gh-4983-tnt-e-assert-false-hangs', function(test)
    test:plan(2)

    local pid = tonumber(io.popen(line):read('*line'))
    assert(pid, 'pid of proccess can\'t be recieved')

    local process_completed = process_timeout.wait_process_completion(pid,
            process_waiting_timeout)

    test:ok(process_completed,
            ('tarantool process with pid = %d completed'):format(pid))

    -- Kill process if hangs.
    if not process_completed then ffi.C.kill(pid, 9) end

    local fh = process_timeout.open_with_timeout(output_file, file_open_timeout)
    assert(fh, 'error while opening ' .. output_file)

    local data = process_timeout.read_with_timeout(fh, file_read_timeout, file_read_interval)
    test:like(data, 'assertion failed', 'assertion failure is displayed')

    fh:close()
    os.remove(output_file)
end)

os.exit(res and 0 or 1)
