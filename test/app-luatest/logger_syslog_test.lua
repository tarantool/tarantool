local fio = require('fio')
local socket = require('socket')
local fiber = require('fiber')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local t = require('luatest')

local g = t.group()

g.test_gh_11840_no_recursive_reconnect = function()
    local dir = treegen.prepare_directory({}, {})
    local socket_path = fio.pathjoin(dir, 'socket')
    local socket = socket('AF_UNIX', 'SOCK_DGRAM', 0)
    socket:bind('unix/', socket_path)

    local script = string.format([[
        local log = require('log')
        box.cfg{
            log = 'syslog:server=unix:%s',
            log_level = 'debug',
        }
        -- Logging should not cause stack overflow. This or
        -- that is done during recovery.
        log.info('Бу!')
        log.info('Испугался?')
        os.exit(0)
    ]], socket_path)
    treegen.write_file(dir, 'script.lua', script)

    fiber.create(function()
        socket:readable()
        socket:close()
    end)

    local res = justrun.tarantool(dir, {}, {'script.lua'},
                                  {nojson = true, stderr = true})
    t.assert_equals(res.exit_code, 0, res.stderr)
end
