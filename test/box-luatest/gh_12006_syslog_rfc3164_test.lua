local fio = require('fio')
local json = require('json')
local server = require('luatest.server')
local socket = require('socket')
local t = require('luatest')

local g = t.group()

local VARDIR = fio.abspath(os.getenv('VARDIR') or 'test/var')
local SOCK_PATH = fio.pathjoin(VARDIR, 'syslog.sock')
local TEST_NAME = 'gh_12006_syslog_rfc3164_test'

g.before_all(function(cg)
    cg.sock = socket('AF_UNIX', 'SOCK_DGRAM', 0)
    t.assert(cg.sock)
    fio.unlink(SOCK_PATH)
    t.assert(cg.sock:bind('unix/', SOCK_PATH))
    cg.server = server:new({
        alias = 'default',
        box_cfg = {
            log = string.format('syslog:server=unix:%s,identity=tt', SOCK_PATH),
            log_level = 'warn',
        },
    })
    cg.server:start()
    cg.pid = tonumber(cg.server.process.pid)
end)

g.after_all(function(cg)
    cg.server:drop()
    cg.sock:close()
    fio.unlink(SOCK_PATH)
end)

local function log(cg, msg)
    cg.server:exec(function(msg)
        require('log').warn(msg)
    end, {msg})
end

local function str_check_no_prohibited_char(str)
    t.assert(str)
    for i = 1, #str do
        local byte = str:byte(i)
        t.assert(byte >= 32 and byte <= 126)
    end
end

local function syslog_check_plain(cg, expected_msg)
    local str = cg.sock:recv(16 * 1024)
    str_check_no_prohibited_char(str)
    local hdr, msg = unpack(str:split(' W> ', 1))
    t.assert_str_matches(
        hdr,
        '<%d+>%a+%s+%d%d?%s+%d%d:%d%d:%d%d%s+tt%[' .. cg.pid .. '%]:%s' ..
        'main/%d+/main/test%.box%-luatest%.' .. TEST_NAME ..  '%s' ..
        TEST_NAME .. '%.lua:37')
    t.assert_equals(msg, expected_msg)
end

local function str_all_bytes()
    local msg = ""
    -- \0 means end of string, not added.
    for b = 1, 255 do
        msg = msg .. string.char(b)
    end
    return msg
end

local function syslog_char2escape()
    local syslog_char2escape = {}
    -- 0–31
    for b = 0, 31 do
        syslog_char2escape[b] = string.format("\\u%04x", b)
    end
    -- special ASCII escapes
    syslog_char2escape[8]  = "\\b"
    syslog_char2escape[9]  = "\\t"
    syslog_char2escape[10] = "\\n"
    syslog_char2escape[11] = "\\u000b"
    syslog_char2escape[12] = "\\f"
    syslog_char2escape[13] = "\\r"
    -- 32–126 → NULL (allowed)
    for b = 32, 126 do
        syslog_char2escape[b] = nil
    end
    -- 127–255
    for b = 127, 255 do
        syslog_char2escape[b] = string.format("\\u%04x", b)
    end
    return syslog_char2escape
end

local function str_make_expected(str, char2escape)
    local msg = ""
    for i = 1, #str do
        local esc = char2escape[str:byte(i)]
        if esc ~= nil then
            msg = msg .. esc
        else
            msg = msg .. str:sub(i, i)
        end
    end
    return msg
end

g.test_syslog_plain = function(cg)
    -- No newline at the end of plain messages, when syslog is used.
    log(cg, 'test plain small')
    syslog_check_plain(cg, 'test plain small')

    -- Check, that all forbidden chars are escaped.
    log(cg, str_all_bytes())
    local expected = str_make_expected(str_all_bytes(), syslog_char2escape())
    syslog_check_plain(cg, expected)
end

local function syslog_check_json(cg, expected_msg)
    local str = cg.sock:recv(16 * 1024)
    str_check_no_prohibited_char(str)
    local hdr, body = unpack(str:split(': ', 1))

    --
    -- Header.
    --
    t.assert_str_matches(
        hdr, '<%d+>%a+%s+%d%d?%s+%d%d:%d%d:%d%d%s+tt%[' .. cg.pid .. '%]')

    --
    -- Message. lua_cjson decodes to unicode, so compare before decode. The
    -- first match is used for string logging, the second - for table.
    --
    local actual = body:match('"message"%s*:%s*"(.-)"%s*,') or
                   body:match('\\"message\\"%s*:%s*\\"(.-)\\"%s*,')
    t.assert_equals(actual, expected_msg)

    --
    -- Body.
    --
    local ok, result = pcall(json.decode, body)
    t.assert(ok)
    result.message = nil
    t.assert_str_matches(
        result.time, '%d%d%d%d%-%d%d%-%d%dT%d%d:%d%d:%d%d%.%d%d%d[+-]%d+')
    result.time = nil
    t.assert_is(type(result.fiber_id), 'number')
    result.fiber_id = nil
    t.assert_str_matches(result.file, '.*' .. TEST_NAME .. '%.lua')
    result.file = nil
    t.assert_equals(result, {
        cord_name = 'main',
        fiber_name = 'main',
        line = 37,
        level = 'WARN',
        module = 'test.box-luatest.' .. TEST_NAME,
        pid = cg.pid,
    })
end

local function json_char2escape()
    local char2escape = syslog_char2escape()
    char2escape[string.byte('"')] = '\\\"'
    char2escape[string.byte('\\')] = '\\\\'
    return char2escape
end

g.test_syslog_json = function(cg)
    cg.server:exec(function() box.cfg({log_format = 'json'}) end)

    log(cg, 'test plain small')
    syslog_check_json(cg, 'test plain small')

    log(cg, str_all_bytes())
    local expected = str_make_expected(str_all_bytes(), json_char2escape())
    syslog_check_json(cg, expected)

    log(cg, {message = str_all_bytes()})
    local expected = str_make_expected(str_all_bytes(), json_char2escape())
    syslog_check_json(cg, expected)

    cg.server:exec(function() box.cfg({log_format = 'plain'}) end)
end
