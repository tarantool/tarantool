local fio = require('fio')
local json = require('json')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')

local g = t.group()

local VARDIR = fio.abspath(os.getenv('VARDIR') or 'test/var')
local SOCK_PATH = fio.pathjoin(VARDIR, 'syslog.sock')

g.before_all(function(cg)
    cg.sock = socket('AF_UNIX', 'SOCK_DGRAM', 0)
    t.assert(cg.sock)
    fio.unlink(SOCK_PATH)
    t.assert(cg.sock:bind('unix/', SOCK_PATH))
    cg.server = server:new({
        alias = 'default',
        box_cfg = {
            log = string.format('syslog:server=unix:%s,identity=tt', SOCK_PATH),
            log_format = 'json',
            log_level = 'warn',
        },
    })
    cg.server:start()
    cg.pid = tonumber(cg.server.process.pid)
    cg.log = function(msg)
        cg.server:exec(function(msg)
            require('log').warn(msg)
        end, {msg})
    end
    cg.check_json = function(expected_msg)
        local s = cg.sock:recv(1024)
        t.assert(s)
        local hdr, body = unpack(s:split(': ', 1))
        t.assert_str_matches(
            hdr, '<%d+>%a+%s+%d%d?%s+%d%d:%d%d:%d%d%s+tt%[' .. cg.pid .. '%]')
        local ok, result = pcall(json.decode, body)
        t.assert(ok)
        t.assert_str_matches(
            result.time, '%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%d%.%d%d%d[+-]%d+')
        result.time = nil
        t.assert_is(type(result.fiber_id), 'number')
        result.fiber_id = nil
        t.assert_str_matches(result.file, '.*gh_7860_syslog_json_test%.lua')
        result.file = nil
        t.assert_equals(result, {
            cord_name = 'main',
            fiber_name = 'main',
            line = 29,
            level = 'WARN',
            message = expected_msg,
            pid = cg.pid,
        })
    end
    cg.check_plain = function(expected_msg)
        local s = cg.sock:recv(1024)
        t.assert(s)
        local hdr, msg = unpack(s:split(' W> ', 1))
        t.assert_str_matches(
            hdr,
            '<%d+>%a+%s+%d%d?%s+%d%d:%d%d:%d%d%s+tt%[' .. cg.pid .. '%]:%s' ..
            'main/%d+/main%sgh_7860_syslog_json_test%.lua:29')
        t.assert_equals(msg, expected_msg .. '\n')
    end
end)

g.test_syslog_json = function(cg)
    cg.log('test json 1')
    cg.check_json('test json 1')
    cg.server:exec(function() box.cfg({log_format = 'plain'}) end)
    cg.log('test plain 1')
    cg.check_plain('test plain 1')
    cg.server:exec(function() box.cfg({log_format = 'json'}) end)
    cg.log('test json 2')
    cg.check_json('test json 2')
    cg.server:exec(function() require('log').cfg({format = 'plain'}) end)
    cg.log('test plain 2')
    cg.check_plain('test plain 2')
    cg.server:exec(function() require('log').cfg({format = 'json'}) end)
    cg.log('test json 3')
    cg.check_json('test json 3')
end

g.after_all(function(cg)
    cg.server:drop()
    cg.sock:close()
    fio.unlink(SOCK_PATH)
end)
