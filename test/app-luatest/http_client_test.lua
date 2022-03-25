local client = require('http.client')
local json = require('json')
local fiber = require('fiber')
local socketlib = require('socket')
local os = require('os')
local t = require('luatest')
local g = t.group('http_client', {
    {sock_family = 'AF_INET'},
    {sock_family = 'AF_UNIX'},
})

local TARANTOOL_SRC_DIR = os.getenv("TARANTOOL_SRC_DIR") or "../.."

local function merge(...)
    local res = {}
    for i = 1, select('#', ...) do
        local t = select(i, ...)
        for k, v in pairs(t) do
            res[k] = v
        end
    end
    return res
end

local function start_server(sock_family, sock_addr)
    local arg, url, opts
    if sock_family == 'AF_INET' then
        arg = string.format("--inet %s", sock_addr)
        url = string.format("http://%s/", sock_addr)
        opts = {}
    elseif sock_family == 'AF_UNIX' then
        arg = string.format("--unix %s", sock_addr)
        url = "http://localhost/"
        opts = {unix_socket = sock_addr}
    else
        error(string.format('invalid socket family: %s', sock_family))
    end
    -- PYTHON_EXECUTABLE is set in http_client.skipcond.
    local python_executable = os.getenv('PYTHON_EXECUTABLE') or ''
    local cmd_prefix = (python_executable .. ' '):lstrip()
    local cmd = string.format("%s%s/test/app-luatest/httpd.py %s",
                              cmd_prefix, TARANTOOL_SRC_DIR, arg)
    local server = io.popen(cmd)
    t.assert_equals(server:read("*l"), "heartbeat", "server started")
    local r
    for _=1,10 do
        r = client.get(url, merge(opts, {timeout = 0.01}))
        if r.status == 200 then
            break
        end
        fiber.sleep(0.01)
    end
    t.assert_equals(r.status, 200, "connection is ok")
    return server, url, opts
end

g.before_all(function(cg)
    local sock_family = cg.params.sock_family
    local sock_addr
    if sock_family == 'AF_INET' then
        local s = socketlib('AF_INET', 'SOCK_STREAM', 0)
        s:bind('127.0.0.1', 0)
        sock_addr = string.format("%s:%d", s:name().host, s:name().port)
        s:close()
    else
        assert(sock_family == 'AF_UNIX')
        local path = os.tmpname()
        os.remove(path)
        local status = pcall(client.get, 'http://localhost/',
                             {unix_socket = path})
        t.skip_if(not status, "not supported")
        sock_addr = path
        cg.unix_socket_path = path
    end
    cg.server, cg.url, cg.opts = start_server(sock_family, sock_addr)
end)

g.after_all(function(cg)
    cg.server:close()
    if cg.unix_socket_path then
        os.remove(cg.unix_socket_path)
    end
    cg.server, cg.url, cg.opts = nil, nil, nil
end)

g.test_http_client = function(cg)
    -- gh-4136: confusing httpc usage error message
    local url, opts = cg.url, cg.opts
    local ok, err = pcall(client.request, client)
    local usage_err = "request(method, url[, body, [options]])"
    t.assert_equals({ok, err:split(': ')[2]}, {false, usage_err},
                    "test httpc usage error")

    t.assert_equals(rawget(_G, 'http'), nil,
                    "global namespace is not polluted");
    t.assert_equals(rawget(_G, 'http.client'), nil,
                    "global namespace is not polluted");
    local r = client.get(url, opts)
    t.assert_equals(r.status, 200, 'simple 200')
    t.assert_equals(r.reason, 'Ok', '200 - Ok')
    t.assert_equals(r.proto[1], 1, 'proto major http 1.1')
    t.assert_equals(r.proto[2], 1, 'proto minor http 1.1')
    t.assert_str_contains(r.body, "hello", "body")
    t.assert_gt(tonumber(r.headers["content-length"]), 0, "content-length > 0")
    t.assert_equals(client.get("http://localhost:1/").status, 595,
                    'cannot connect')

    local r = client.request('GET', url, nil, opts)
    t.assert_equals(r.status, 200, 'request')
end

g.test_follow_location = function(cg)
    -- gh-4119: specify whether to follow 'Location' header
    local url, opts = cg.url, cg.opts
    local endpoint = 'redirect'

    -- Verify that the default behaviour is to follow location.
    local r = client.request('GET', url .. endpoint, nil, opts)
    t.assert_equals(r.status, 200, 'default: status')
    t.assert_equals(r.body, 'hello world', 'default: body')
    -- gh-6101: headers are reset on redirect
    r.headers.date = r.headers.date and 'DATE'
    r.headers.host = r.headers.host and 'HOST'
    t.assert_equals(r.headers, {
        ['date'] = 'DATE',
        ['host'] = 'HOST',
        ['accept'] = '*/*',
        ['connection'] = 'close',
        ['content-length'] = '11',
        ['content-type'] = 'application/json',
    }, 'default: headers')

    -- Verify {follow_location = true} behaviour.
    local r = client.request('GET', url .. endpoint, nil, merge(opts, {
                             follow_location = true}))
    t.assert_equals(r.status, 200, 'follow location: status')
    t.assert_equals(r.body, 'hello world', 'follow location: body')
    -- gh-6101: headers are reset on redirect
    r.headers.date = r.headers.date and 'DATE'
    r.headers.host = r.headers.host and 'HOST'
    t.assert_equals(r.headers, {
        ['date'] = 'DATE',
        ['host'] = 'HOST',
        ['accept'] = '*/*',
        ['connection'] = 'close',
        ['content-length'] = '11',
        ['content-type'] = 'application/json',
    }, 'follow location: headers')

    -- Verify {follow_location = false} behaviour.
    local r = client.request('GET', url .. endpoint, nil, merge(opts, {
                             follow_location = false}))
    t.assert_equals(r.status, 302, 'do not follow location: status')
    t.assert_equals(r.body, 'redirecting...',
                    'do not follow location: body')
    -- gh-6101: headers are not reset if redirect is disabled
    r.headers.date = r.headers.date and 'DATE'
    r.headers.host = r.headers.host and 'HOST'
    t.assert_equals(r.headers, {
        ['date'] = 'DATE',
        ['host'] = 'HOST',
        ['location'] = '/',
        ['accept'] = '*/*',
        ['connection'] = 'close',
        ['content-length'] = '14',
    }, 'do not follow location: headers')
end

--
-- gh-3955: Check that httpc module doesn't redefine http headers
--          set explicitly by the caller.
--
g.test_http_client_headers_redefine = function(cg)
    local url = cg.url
    local opts = table.deepcopy(cg.opts)
    -- Test defaults
    opts.headers = {['Connection'] = nil, ['Accept'] = nil}
    local r = client.post(url, nil, opts)
    t.assert_equals(r.status, 200, 'simple 200')
    t.assert_equals(r.headers['connection'], 'close',
                    'Default Connection header')
    t.assert_equals(r.headers['accept'], '*/*',
                    'Default Accept header for POST request')
    -- Test that in case of conflicting headers, user variant is
    -- prefered
    opts.headers={['Connection'] = 'close'}
    opts.keepalive_idle = 2
    opts.keepalive_interval = 1
    local r = client.get(url, opts)
    t.assert_equals(r.status, 200, 'simple 200')
    t.assert_equals(r.headers['connection'], 'close',
                    'Redefined Connection header')
    t.assert_equals(r.headers['keep_alive'], 'timeout=2',
                    'Automatically set Keep-Alive header')
    -- Test that user-defined Connection and Acept headers
    -- are used
    opts.headers={['Connection'] = 'Keep-Alive', ['Accept'] = 'text/html'}
    local r = client.get(url, opts)
    t.assert_equals(r.status, 200, 'simple 200')
    t.assert_equals(r.headers['accept'], 'text/html',
                    'Redefined Accept header')
    t.assert_equals(r.headers['connection'], 'Keep-Alive',
                    'Redefined Connection header')
end

g.test_cancel_and_errinj = function(cg)
    local url, opts = cg.url, cg.opts
    local ch = fiber.channel(1)
    local http = client:new()
    local func  = function(fopts)
        ch:put(http:get(url, fopts))
    end
    local f = fiber.create(func, opts)
    f:cancel()
    local r = ch:get()
    t.assert_equals(r.status, 408,
                    "After cancel fiber timeout is returned - status")
    t.assert_str_contains(r.reason, "Timeout",
                          "After cancel fiber timeout is returned - reason")
    r = http:get(url .. 'long_query', merge(opts, {timeout = 0.0001}))
    t.assert_equals(r.status, 408, "Timeout check - status")
    t.assert_str_contains(r.reason, "Timeout", "Timeout check - reason")
    local errinj = box.error.injection
    errinj.set('ERRINJ_HTTP_RESPONSE_ADD_WAIT', true)
    local topts = merge(opts, {timeout = 1200})
    fiber.create(func, topts)
    r = ch:get()
    t.assert_equals(r.status, 200, "No hangs in errinj")
    errinj.set('ERRINJ_HTTP_RESPONSE_ADD_WAIT', false)
end

g.test_post_and_get = function(cg)
    local http = client.new()
    t.assert(http ~= nil, "client is created")

    local headers = { header1 = "1", header2 = "2" }
    local my_body = { key = "value" }
    local json_body = json.encode(my_body)
    local responses = {}
    headers['Content-Type'] = 'application/json'
    local fibers = 7
    local ch = fiber.channel(fibers)
    local url = cg.url
    local opts = merge(cg.opts, {headers = headers})
    fiber.create(function()
        responses.good_get = http:get(url, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.get2 = http:get(url .. "abc", opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.good_post = http:post(url, json_body, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.empty_post = http:post(url, nil, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.good_put = http:put(url, json_body, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.bad_get = http:get(url .. 'this/page/not/exists', opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.absent_get = http:get(url .. 'absent', opts)
        ch:put(1)
    end)
    for _=1,fibers do
        ch:get()
    end
    local r = responses.good_get
    t.assert_equals(r.status, 200, "GET: default http code page exists")
    t.assert_equals(r.body, "hello world", "GET: default right body")

    r = responses.get2
    t.assert_equals(r.status, 200, "GET: http code page exists")
    t.assert_equals(r.body, "abc", "GET: right body")

    r = responses.absent_get
    t.assert_equals(r.status, 500, "GET: absent method http code page exists")
    t.assert_equals(r.reason, 'Unknown', '500 - Unknown')
    t.assert_equals(r.body, "No such method", "GET: absent method right body")

    r = responses.empty_post
    t.assert_equals(r.status, 200, "POST: good status")
    t.assert(r.headers['header1'] == headers.header1 and
             r.headers['header2'] == headers.header2, "POST: good headers")
    t.assert_equals(r.body, nil, "POST: empty body")

    r = responses.good_post
    t.assert_equals(r.status, 200, "POST: good status")
    t.assert_equals(r.headers['header1'], headers.header1,
                    "POST: good header 1")
    t.assert_equals(r.headers['header2'], headers.header2,
                    "POST: good header 2")
    t.assert_equals(r.body, json_body, "POST: body")

    r = responses.good_put
    t.assert_equals(r.status, 200, "PUT: good status")
    t.assert_equals(r.headers['header'], headers.header,
                    "PUT: good header 1")
    t.assert_equals(r.headers['header2'], headers.header2,
                    "PUT: good header 2")

    r = responses.bad_get
    t.assert_equals(r.status, 404, "GET: http page not exists")
    t.assert_equals(r.reason, 'Unknown', '404 - Unknown')
    t.assert(r.body:len() ~= 0, "GET: not empty body page not exists")
    t.assert_str_contains(r.body, "Not Found",
                          "GET: right body page not exists")

    local st = http:stat()
    t.assert_equals(st.sockets_added, st.sockets_deleted,
                    "All sockets deleted")
    t.assert_equals(st.active_requests, 0, "No active requests")
end

g.test_errors = function()
    local http = client:new()
    local status, err = pcall(http.get, http, "htp://mail.ru")
    t.assert_not(status, "GET: exception on bad protocol - status")
    t.assert_str_contains(json.encode(err), "Unsupported protocol",
                          "GET: exception on bad protocol - error")
    status, err = pcall(http.post, http, "htp://mail.ru", "")
    t.assert_not(status, "POST: exception on bad protocol - status")
    t.assert_str_contains(json.encode(err), "Unsupported protocol",
                          "POST: exception on bad protocol - error")
end

-- gh-3679 Check that opts.headers values can be strings only.
-- gh-4281 Check that opts.headers can be a table and opts.headers
-- keys can be strings only.
g.test_request_headers = function(cg)
    local url, opts = cg.url, cg.opts
    local exp_err_bad_opts_headers = 'opts.headers should be a table'
    local exp_err_bad_key = 'opts.headers keys should be strings'
    local exp_err_bad_value = 'opts.headers values should be strings'
    local cases = {
        -- Verify opts.headers type checks.
        {
            'string opts.headers',
            opts = {headers = 'aaa'},
            exp_err = exp_err_bad_opts_headers,
        },
        {
            'number opts.headers',
            opts = {headers = 1},
            exp_err = exp_err_bad_opts_headers,
        },
        {
            'cdata (box.NULL) opts.headers',
            opts = {headers = box.NULL},
            exp_err = exp_err_bad_opts_headers,
        },
        -- Verify a header key type checks.
        {
            'number header key',
            opts = {headers = {[1] = 'aaa'}},
            exp_err = exp_err_bad_key,
        },
        {
            'boolean header key',
            opts = {headers = {[true] = 'aaa'}},
            exp_err = exp_err_bad_key,
        },
        {
            'table header key',
            opts = {headers = {[{}] = 'aaa'}},
            exp_err = exp_err_bad_key,
        },
        {
            'cdata header key (box.NULL)',
            opts = {headers = {[box.NULL] = 'aaa'}},
            exp_err = exp_err_bad_key,
        },
        -- Verify a header value type checks.
        {
            'string header key & value',
            opts = {headers = {aaa = 'aaa'}},
            exp_err = nil,
        },
        {
            'boolean header value',
            opts = {headers = {aaa = true}},
            exp_err = exp_err_bad_value,
        },
        {
            'number header value',
            opts = {headers = {aaa = 10}},
            exp_err = exp_err_bad_value,
        },
        {
            'cdata header value (box.NULL)',
            opts = {headers = {aaa = box.NULL}},
            exp_err = exp_err_bad_value,
        },
        {
            'cdata<uint64_t> header value',
            opts = {headers = {aaa = 10ULL}},
            exp_err = exp_err_bad_value,
        },
        {
            'table header value',
            opts = {headers = {aaa = {}}},
            exp_err = exp_err_bad_value,
        },
    }

    local http = client:new()

    for _, case in ipairs(cases) do
        opts = merge(table.copy(opts), case.opts)
        local ok, err = pcall(http.get, http, url, opts)
        if case.postrequest_check ~= nil then
            case.postrequest_check(opts)
        end
        if case.exp_err == nil then
            -- expect success
            t.assert(ok, case[1])
        else
            -- expect fail
            assert(type(err) == 'string')
            err = err:gsub('^builtin/[a-z._]+.lua:[0-9]+: ', '')
            t.assert_equals({ok, err}, {false, case.exp_err}, case[1])
        end
    end
end

g.test_headers = function(cg)
    local url, opts = cg.url, cg.opts
    local http = client:new()
    local r = http:get(url .. 'headers', opts)
    t.assert_equals(type(r.headers["set-cookie"]), 'string', "set-cookie check")
    t.assert_str_contains(r.headers["set-cookie"], "likes=cheese",
                          "set-cookie check")
    t.assert_str_contains(r.headers["set-cookie"], "age = 17",
                          "set-cookie check")
    t.assert_equals(r.headers["content-type"], "application/json",
                    "content-type check")
    t.assert_equals(r.headers["my_header"], "value1,value2",
                    "other header check")
    t.assert_equals(r.headers["11200ok"], nil,
                    "http status line not included in headers")
    t.assert_equals(r.cookies["likes"][1], "cheese", "cookie value check")
    t.assert_str_contains(r.cookies["likes"][2][1], "Expires",
                          "cookie option check")
    t.assert_str_contains(r.cookies["likes"][2][3], "HttpOnly",
                          "cookie option check")
    t.assert_equals(r.cookies["age"][1], "17", "cookie value check")
    t.assert_equals(#r.cookies["age"][2], 1, "cookie option check")
    t.assert_equals(r.cookies["age"][2][1], "Secure", "cookie option check")
    t.assert(r.cookies["good_name"] ~= nil , "cookie name check")
    t.assert_equals(r.cookies["bad@name"], nil , "cookie name check")
    t.assert_equals(r.cookies["badname"], nil , "cookie name check")
    t.assert_equals(r.cookies["badcookie"], nil , "cookie name check")
    t.assert_equals(r.headers["very_very_very_long_headers_name1"], nil,
                    "no long header name")
    t.assert_equals(r.headers["very_very_very_long_headers_name"], "true",
                    "truncated name")
    opts["max_header_name_length"] = 64
    local r = http:get(url .. 'headers', opts)
    t.assert_equals(r.headers["very_very_very_long_headers_name1"], "true",
                    "truncated max_header_name_length")
    opts["max_header_name_length"] = nil

    -- Send large headers.
    local MAX_HEADER_NAME = 8192
    local hname = 'largeheader'

    -- "${hname}: ${hvalue}" is 8192 bytes length
    local hvalue = string.rep('x', MAX_HEADER_NAME - hname:len() - 2)
    local headers = {[hname] = hvalue}
    local r = http:post(url, nil, merge(opts, {headers = headers}))
    t.assert_equals(r.headers[hname], hvalue, '8192 bytes header: success')

    -- "${hname}: ${hvalue}" is 8193 bytes length
    local exp_err = 'header is too large'
    local hvalue = string.rep('x', MAX_HEADER_NAME - hname:len() - 1)
    local headers = {[hname] = hvalue}
    local ok, err = pcall(http.post, http, url, nil,
                          merge(opts, {headers = headers}))
    t.assert_equals({ok, tostring(err)}, {false, exp_err},
                    '8193 KiB header: error')
end

g.test_special_methods = function(cg)
    local url, opts = cg.url, cg.opts
    local http = client.new()
    local responses = {}
    local fibers = 7
    local ch = fiber.channel(fibers)
    fiber.create(function()
        responses.patch_data = http:patch(url, "{\"key\":\"val\"}", opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.delete_data = http:delete(url, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.options_data = http:options(url, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.head_data = http:head(url, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.trace_data = http:trace(url, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.connect_data = http:connect(url, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.custom_data = http:request("CUSTOM", url, nil, opts)
        ch:put(1)
    end)
    for _ = 1, fibers do
        ch:get()
    end

    t.assert_equals(responses.patch_data.status, 200, "HTTP:PATCH request")
    t.assert_equals(json.decode(responses.patch_data.body).key, "val",
                    "HTTP:PATCH request content")
    t.assert_equals(responses.delete_data.status, 200, "HTTP:DELETE request")
    t.assert_equals(responses.delete_data.headers.method, "DELETE",
                    "HTTP:DELETE request content")
    t.assert_equals(responses.options_data.status, 200, "HTTP:OPTIONS request")
    t.assert_equals(responses.options_data.headers.method, "OPTIONS",
                    "HTTP:OPTIONS request content")
    t.assert_equals(responses.head_data.status, 200, "HTTP:HEAD request code")
    t.assert_equals(responses.head_data.headers.method, "HEAD",
                    "HTTP:HEAD request content")
    t.assert_equals(responses.connect_data.status, 200, "HTTP:CONNECT request")
    t.assert_equals(responses.connect_data.headers.method, "CONNECT",
                    "HTTP:OPTIONS request content")
    t.assert_equals(responses.trace_data.status, 200, "HTTP:TRACE request")
    t.assert_equals(responses.trace_data.headers.method, "TRACE",
                    "HTTP:TRACE request content")
    t.assert_equals(responses.custom_data.status, 400, "HTTP:CUSTOM request")
    t.assert_equals(responses.custom_data.headers.method, "CUSTOM",
                    "HTTP:CUSTOM request content")
end

g.test_concurrent = function(cg)
    --
    -- BSD-based operating systems (including OS X) will fail connect() to a
    -- Unix domain socket with ECONNREFUSED if the queue of pending connections
    -- is full. Hence the "concurrent" test, which opens a lot of connections
    -- simultaneously, cannot run on those platforms. Linux, however, is fine -
    -- instead of returning ECONNEREFUSED it will suspend connect() until
    -- backlog is processed.
    --
    t.skip_if(cg.params.sock_family == 'AF_UNIX' and jit.os ~= 'Linux',
              "Linux-specific")

    local url, opts = cg.url, cg.opts
    local num_test = 10
    local num_load = 10
    local curls   = { }
    local headers = { }

    -- Init [[
    for i = 1, num_test do
        headers["My-header" .. i] = "my-value"
    end

    for _ = 1, num_test do
        table.insert(curls, {
            url = url,
            http = client.new(),
            body = json.encode({stat = {"ok"},
            info = {"ok"} }),
            headers = headers,
            connect_timeout = 5,
            timeout = 5
        })
    end
    -- ]]

    local ch = fiber.channel(num_test * 2 * num_load)
    -- Start test
    -- Creating concurrent clients
    for i=1,num_test do
        local obj = curls[i]
        for _=1,num_load do
            fiber.create(function()
                local r = obj.http:post(obj.url, obj.body, merge(opts, {
                    headers = obj.headers,
                    keepalive_idle = 30,
                    keepalive_interval = 60,
                    connect_timeout = obj.connect_timeout,
                    timeout = obj.timeout,
                }))
                ch:put(r.status)
            end)
            fiber.create(function()
                local r = obj.http:get(obj.url, merge(opts, {
                    headers = obj.headers,
                    keepalive_idle = 30,
                    keepalive_interval = 60,
                    connect_timeout = obj.connect_timeout,
                    timeout = obj.timeout,
                }))
                ch:put(r.status)
            end)
        end
    end
    local ok_sockets_added = true
    local ok_active = true
    local ok_req = true

    -- Join test
    local rest = num_test
    while true do
        for i = 1, num_load do
            local obj = curls[i]
            -- checking that stats in concurrent are ok
            if obj.http ~= nil and obj.http:stat().active_requests == 0 then
                local st = obj.http:stat()
                if st.sockets_added ~= st.sockets_deleted then
                    ok_sockets_added = false
                    rest = 0
                end
                if st.active_requests ~= 0 then
                    ok_active = false
                    rest = 0
                end
                -- waiting requests to finish before kill the client
                local r = ch:get()
                if r ~= 200 then
                    ok_req = false
                end
                r = ch:get()
                if r ~= 200 then
                    print(r)
                end
            end
            curls[i].http = nil
        end
        rest = rest - 1
        if rest <= 0 then
            break
        end
    end
    t.assert(ok_req, "All requests are ok")
    t.assert(ok_sockets_added, "free sockets")
    t.assert(ok_active, "no active requests")
end
