#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test("gh-4450-log-rotate-exit")
local fio = require('fio')
local tarantool_bin = arg[-1]

test:plan(1)

local function run_script(code)
    local dir = fio.tempdir()
    local script_path = fio.pathjoin(dir, 'gh-4450-script.lua')
    local script = fio.open(script_path,
        {'O_CREAT', 'O_WRONLY', 'O_APPEND'},
        tonumber('0777', 8))
    script:write(code)
    script:close()
    local output_file = fio.pathjoin(fio.cwd(), 'gh-4450-out.txt')
    local cmd = [[/bin/sh -c 'cd "%s" && "%s" ./gh-4450-script.lua 0> %s 2> %s']]
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
local code = "log = require('log')\n"
code = code .. "box.cfg{log = 'gh-4550.log'}\n"
code = code .. "box.error.injection.set('ERRINJ_LOG_ROTATE', true)\n"
code = code .. "log.rotate()\n"
code = code .. "os.exit()\n"
local _, output = run_script(code)

test:diag(output)
test:ok(not string.find(output, "SEGV_MAPERR"),
        "program exited without SEGV_MAPERR")

test:check()
os.exit(0)
