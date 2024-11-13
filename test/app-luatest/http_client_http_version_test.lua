-- This tests are using treegen to catch information about protocol version
-- by catching stderr. Treegen creates and tracks temporary directories, where
-- scripts with testing workloads reside.

local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')
local socket = require('socket')

local wrong_version_group = t.group()

local http_version_group = t.group('versions', {
    { proto = 'http', version = nil },
    { proto = 'http', version = '1.1' },
    { proto = 'http', version = '2' },
    { proto = 'http', version = '2-tls' },
    { proto = 'http', version = '2-prior-knowledge' },
    { proto = 'https', version = nil },
    { proto = 'https', version = '1.1' },
    { proto = 'https', version = '2' },
    { proto = 'https', version = '2-tls' },
    { proto = 'https', version = '2-prior-knowledge' },
})

local expected = {
    ['nil'] = {
        http = 'GET / HTTP/1.1',
        https = 'ALPN: curl offers h2,http/1.1',
    },
    ['1.1'] = {
        http = 'GET / HTTP/1.1',
        https = 'ALPN: curl offers http/1.1',
    },
    ['2'] = {
        http = 'Connection: Upgrade, HTTP2-Settings',
        https = 'ALPN: curl offers h2,http/1.1',
    },
    ['2-tls'] = {
        http = 'GET / HTTP/1.1',
        https = 'ALPN: curl offers h2,http/1.1',
    },
    ['2-prior-knowledge'] = {
        http = '[HTTP/2] [1] OPENED stream',
        https = 'ALPN: curl offers h2',
    },
}

http_version_group.before_each(function(g)
    g.server = socket.tcp_server('127.0.0.1', 0, function(_s) end)
end)

http_version_group.after_each(function(g)
    g.server:close()
end)

local http_request_script = string.dump(function()
    local http_client = require('http.client').new()
    local proto = os.getenv('PROTO')
    local port = os.getenv('HTTP_SERVER_PORT')
    local uri = proto .. '://127.0.0.1:' .. port
    local ok = pcall(http_client.get, http_client, uri, {
        verbose = true,
        http_version = os.getenv('HTTP_VERSION'),
    })
    -- The server accepts the connection and closes it immediately so
    -- the http request is expected to fail.
    assert(not ok)
end)

http_version_group.test_http_version = function(g)
    local dir = treegen.prepare_directory({}, {})
    local script_name = 'http_request.lua'
    treegen.write_file(dir, script_name, http_request_script)
    local args = {script_name}
    local opts = {nojson = true, stderr = true}
    local env = {HTTP_SERVER_PORT = g.server:name().port}
    env.PROTO = g.params.proto
    env.HTTP_VERSION = g.params.version
    local res = justrun.tarantool(dir, env, args, opts)
    local exp = expected[tostring(g.params.version)][g.params.proto]
    t.assert_str_contains(res.stderr, exp)
end

wrong_version_group.test_request_wrong_http_version = function()
    local http_client = require('http.client').new()
    local exp_err = 'Not a supported http version: Not an http version'
    local uri = 'http://127.0.0.1:8080'
    t.assert_error_msg_equals(exp_err, function()
        http_client:get(uri, {http_version = 'Not an http version'})
    end)
end
