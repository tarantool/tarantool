local t = require('luatest')
local fio = require('fio')

local g = t.group('gh-6647-yield-gc-hook')

local tarantool_bin = arg[-1]
local PANIC = 256
local ERRMSG = 'fiber [0-9]+ is switched while running GC finalizer'

local function check_log(t, filename, pattern)
    local file = fio.open(filename, {'O_RDONLY'})
    assert(file ~= nil)

    local size = file:stat().size
    assert(size ~= 0)

    local log_contents = file:pread(size, 0)
    file:close()
    assert(log_contents ~= '')
    t.assert_str_contains(log_contents, pattern, true, 'invalid error message')
end

local function check_case(t, code, logfile, case_name)
    local dir = fio.tempdir()
    local script_path = fio.pathjoin(dir, 'script.lua')
    local script = fio.open(script_path, {'O_CREAT', 'O_WRONLY', 'O_TRUNC'},
        tonumber('0777', 8))
    script:write(code)
    script:write("\nos.exit(0)")
    script:close()
    local cmd = [[/bin/sh -c 'cd "%s" && "%s" ./script.lua 2>&1 /dev/null']]
    local res = os.execute(string.format(cmd, dir, tarantool_bin))

    check_log(t, fio.pathjoin(dir, logfile), ERRMSG)
    t.assert_equals(res, PANIC,
        'fail on fiber switch in GC finalizer: ' .. case_name)

    fio.rmtree(dir)
    return res
end

g.test_lua_iproto_call = function()
    local lua_iproto_call = [[
    box.cfg({listen = 'unix/:./tarantool.sock', log = 'gh-6647.log'})
    box.schema.user.grant('guest', 'super')
    local ffi = require('ffi')
    local fiber = require('fiber')
    local net_box = require('net.box')
    local c = net_box.connect('unix/:./tarantool.sock')
    function f()
        local junk = {__gh_hook = ffi.gc(ffi.new('void *'), fiber.yield)}
        junk = nil
        collectgarbage()
    end
    c:call('f')
    ]]
    check_case(t, lua_iproto_call, 'gh-6647.log', 'lua_iproto_call')
end

g.test_space_trigger = function()
    local space_trigger = [[
    box.cfg({listen = 'unix/:./tarantool.sock', log = 'gh-6647.log'})
    box.schema.user.grant('guest', 'super')
    local ffi = require('ffi')
    local net_box = require('net.box')
    local fiber = require('fiber')

    local function on_replace()
        local junk = {__gh_hook = ffi.gc(ffi.new('void *'), fiber.yield)}
        junk = nil
        collectgarbage()
    end

    box.schema.space.create('myspace')
    box.space.myspace:create_index('test_index',{})
    box.space.myspace:on_replace(on_replace)
    local c = net_box.connect('unix/:./tarantool.sock')
    c.space.myspace:insert{1,'Hi'}
    ]]
    check_case(t, space_trigger, 'gh-6647.log', 'space trigger')
end
