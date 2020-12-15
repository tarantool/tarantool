#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("fail_main")
local fio = require('fio')
local tarantool_bin = arg[-1]

test:plan(1)

local function run_script(code)
    local dir = fio.tempdir()
    local script_path = fio.pathjoin(dir, 'script.lua')
    local script = fio.open(script_path, {'O_CREAT', 'O_WRONLY', 'O_APPEND'},
        tonumber('0777', 8))
    script:write(code)
    script:close()
    local output_file = fio.pathjoin(fio.cwd(), 'out.txt')
    local cmd = [[/bin/sh -c 'cd "%s" && "%s" ./script.lua 0> %s 2> %s']]
    local code = os.execute(
        string.format(cmd, dir, tarantool_bin, output_file, output_file)
    )
    fio.rmtree(dir)
    local out_fd = fio.open(output_file, {'O_RDONLY'})
    local output = out_fd:read(100000)
    out_fd:close()
    return code, output
end

--
-- gh-4382: an error in main script should be handled gracefully,
-- with proper logging.
--
local _, output = run_script("error('Error in the main script')")

test:ok(output:match("fatal error, exiting the event loop"),
        "main script error is handled gracefully")

test:check()
os.exit(0)
