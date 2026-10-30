#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('cfg')
local fio = require('fio')
test:plan(1)

local tarantool_bin = arg[-1]
local PANIC = 256

local function run_script(code)
    local dir = fio.tempdir()
    local script_path = fio.pathjoin(dir, 'script.lua')
    local script = fio.open(script_path, {'O_CREAT', 'O_WRONLY', 'O_APPEND'},
        tonumber('0777', 8))
    script:write(code)
    script:write("\nos.exit(0)")
    script:close()
    local cmd = [[/bin/sh -c 'cd "%s" && "%s" ./script.lua 2> /dev/null']]
    local res = os.execute(string.format(cmd, dir, tarantool_bin))
    fio.rmtree(dir)
    return res
end

--
-- gh-4594: when memtx_dir is not exists, but vinyl_dir exists and
-- errno is set to ENOENT, box configuration succeeds, however it
-- should not
--

-- create vinyl_dir as temporary path
local vinyl_dir = fio.tempdir()

-- fill vinyl_dir
run_script(string.format([[
box.cfg{vinyl_dir = '%s'}
s = box.schema.space.create('test', {engine = 'vinyl'})
s:create_index('pk')
os.exit(0)
]], vinyl_dir))

-- verify the case described above
local code = string.format([[
local errno = require('errno')
errno(errno.ENOENT)
box.cfg{vinyl_dir = '%s'}
os.exit(0)
]], vinyl_dir)
test:is(run_script(code), PANIC, "bootstrap with ENOENT from non-empty vinyl_dir")

-- remove vinyl_dir
fio.rmtree(vinyl_dir)

os.exit(test:check() and 0 or 1)
