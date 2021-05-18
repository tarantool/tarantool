#!/usr/bin/env tarantool

local process_timeout = require('process_timeout')
local ffi = require('ffi')
local tap = require('tap')
local fio = require('fio')

--
-- Tests to check if the tarantool binary enters
-- interactive mode or not using error injections
-- to change return value of isatty(stdin).
--

local TARANTOOL_PATH = arg[-1]
local output_file = fio.abspath('gh-5040_out.txt')
local cmd_end = (' >%s & echo $!'):format(output_file)

-- Like a default timeout for `cond_wait` in test-run
local process_waiting_timeout = 60.0
local file_read_timeout = 60.0
local file_read_interval = 0.2
local file_open_timeout = 60.0

-- Each testcase consists of:
--  * cmd_args - command line arguments for tarantool binary
--  * stdin - stdin for tarantool
--  * interactive - true if interactive mode expected
--  * empty_output - true if command should have empty output
local testcases = {
    {
        cmd_args = '',
        stdin = 'tty',
        interactive = true
    },
    {
        cmd_args = '',
        stdin = '/dev/null',
        interactive = false,
        empty_output = true
    },

    {
        cmd_args = ' -e "print(_VERSION)"',
        stdin = 'tty',
        interactive = false
    },
    {
        cmd_args = ' -e "print(_VERSION)"',
        stdin = '/dev/null',
        interactive = false
    },

    {
        cmd_args = ' -i -e "print(_VERSION)"',
        stdin = 'tty',
        interactive = true
    },
    {
        cmd_args = ' -i -e "print(_VERSION)"',
        stdin = '/dev/null',
        interactive = true
    }
}

local test = tap.test('gh-5040')

test:plan(#testcases)
for _, cmd in pairs(testcases) do
    local full_cmd = ''
    if cmd.stdin == 'tty' then
        cmd.stdin = ''
        full_cmd = 'ERRINJ_STDIN_ISATTY=1 '
    else
        cmd.stdin = '< ' .. cmd.stdin
    end

    local full_cmd = full_cmd .. ('%s %s %s %s'):format(
            TARANTOOL_PATH,
            cmd.cmd_args,
            cmd.stdin,
            cmd_end
    )
    test:test(full_cmd, function(test)
        test:plan(cmd.interactive and 1 or 2)

        local pid = tonumber(io.popen(full_cmd):read("*line"))
        assert(pid, "pipe error for: " .. cmd.cmd_args)

        local fh = process_timeout.open_with_timeout(output_file,
                file_open_timeout)
        assert(fh, 'error while opening ' .. output_file)

        if cmd.interactive then
            local data = process_timeout.read_with_timeout(fh,
                    file_read_timeout,
                    file_read_interval)
            test:like(data, 'tarantool>', 'interactive mode detected')
        else
            local process_completed = process_timeout.wait_process_completion(
                    pid,
                    process_waiting_timeout)
            test:ok(process_completed, 'process completed')

            -- If empty output expected, then don't wait full file_read_timeout
            -- for non-empty output_file, only a little time to be sure that
            -- file is empty.
            local read_timeout = cmd.empty_output and file_read_interval
                    or file_read_timeout
            local data = process_timeout.read_with_timeout(fh, read_timeout,
                    file_read_interval)
            if cmd.empty_output then
                test:ok(#data == 0, 'output is empty')
            else
                test:unlike(data, 'tarantool>',
                        'iteractive mode wasn\'t detected')
            end
        end
        if process_timeout.process_is_alive(pid) then
            ffi.C.kill(pid, 9)
        end
        fh:close()
        os.remove(output_file)
    end)
end

os.exit(test:check() and 0 or 1)
