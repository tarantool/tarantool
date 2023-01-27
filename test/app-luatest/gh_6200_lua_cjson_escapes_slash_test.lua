local server = require('luatest.server')
local t = require('luatest')

local function server_test_json_encode()
    local compat = require('tarantool').compat
    local json = require('json')

    -- Test that '/' is escaped with default setting.
    t.assert_equals(json.encode({url = 'https://srv:7777'}),
                    '{"url":"https:\\/\\/srv:7777"}')
    t.assert_equals(json.encode('/home/user/tarantool'),
                                [["\/home\/user\/tarantool"]])
    -- Test that other escape symbols are not affected by the setting.
    t.assert_equals(json.encode('\t'), [["\t"]])
    t.assert_equals(json.encode('\\'), [["\\"]])

    compat.json_escape_forward_slash = 'old'
    -- Test that '/' is escaped with 'old' setting.
    t.assert_equals(json.encode({url = 'https://srv:7777'}),
                    '{"url":"https:\\/\\/srv:7777"}')
    t.assert_equals(json.encode('/home/user/tarantool'),
                    [["\/home\/user\/tarantool"]])
    -- Test that other escape symbols are not affected by the setting.
    t.assert_equals(json.encode('\t'), [["\t"]])
    t.assert_equals(json.encode('\\'), [["\\"]])

    compat.json_escape_forward_slash = 'new'
    -- Test that '/' is not escaped with 'new' setting.
    t.assert_equals(json.encode({url = 'https://srv:7777'}),
                                [[{"url":"https://srv:7777"}]])
    t.assert_equals(json.encode('/home/user/tarantool'),
                    [["/home/user/tarantool"]])
    -- Test that other escape symbols are not affected by the setting.
    t.assert_equals(json.encode('\t'), [["\t"]])
    t.assert_equals(json.encode('\\'), [["\\"]])

    -- Restore options defaults.
    compat.json_escape_forward_slash = 'default'
    -- Test that default is restored.
    t.assert_equals(json.encode({url = 'https://srv:7777'}),
                    '{"url":"https:\\/\\/srv:7777"}')
    t.assert_equals(json.encode('/home/user/tarantool'),
                    [["\/home\/user\/tarantool"]])
end

local function server_test_json_new_encode()
    local compat = require('tarantool').compat
    local json = require('json')

    compat.json_escape_forward_slash = 'old'
    -- Test that '/' is escaped with 'old' setting.
    t.assert_equals(json.encode('/'), [["\/"]])

    -- Test that a serializer created with json.new() follows
    -- the compat configuration.
    local json_old = json.new()
    t.assert_equals(json_old.encode('/'), [["\/"]])
    compat.json_escape_forward_slash = 'new'
    t.assert_equals(json_old.encode('/'), [["/"]])
    t.assert_equals(json.encode('/'), [["/"]])
    local json_new = json.new()
    t.assert_equals(json_new.encode('/'), [["/"]])

    -- Restore options defaults.
    compat.json_escape_forward_slash = 'default'
end

local function popen_test_json_log()
    local popen = require('popen')
    local clock = require('clock')

    -- Start external tarantool session.
    local TARANTOOL_PATH = arg[-1]
    local cmd = TARANTOOL_PATH .. ' -i 2>&1'
    local ph = popen.new({cmd}, {
        shell = true,
        setsid = true,
        group_signal = true,
        stdout = popen.opts.PIPE,
        stderr = popen.opts.PIPE,
        stdin = popen.opts.PIPE,
    })
    t.assert(ph, 'process is not up')

    -- Set up compat and log.cfg.
    ph:write('require("tarantool").compat.json_escape_forward_slash = "old"\n')
    ph:write('require("log").cfg{format = "json"}\n')

    -- Test old log.info behavior.
    ph:write('require("log").info("/")\n')
    local output = '';
    local time_quota = 2.0
    local start_time = clock.monotonic();
    while output:find('message') == nil
        and clock.monotonic() - start_time < time_quota do
        local data = ph:read({timeout = 1.0})
        if data ~= nil then
            output = output .. data
        end
    end

    t.assert_str_contains(output, '"message": "\\/"')

    ph:write('require("tarantool").compat.json_escape_forward_slash = "new"\n')
    start_time = clock.monotonic();
    output = ''
    -- Test new log.info behavior.
    ph:write('require("log").info("/")\n')
    while output:find('message') == nil
        and clock.monotonic() - start_time < time_quota do
        local data = ph:read({timeout = 1.0})
        if data ~= nil then
            output = output .. data
        end
    end

    t.assert_str_contains(output, '"message": "/"')

    ph:close()
end

local tests = {
    server_test_json_encode,
    server_test_json_new_encode,
    popen_test_json_log,
}

local g = t.group('pgroup', t.helpers.matrix{test_func = tests})

g.test_json_encode = function(cg)
    local s = server:new{alias = 'default'}
    s:start()
    s:exec(cg.params.test_func)
    s:stop()
end
