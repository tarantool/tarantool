#!/usr/bin/env tarantool

local tap = require('tap')
local client = require('http.client')
local json = require('json')
local test = tap.test("curl")
local fiber = require('fiber')
local socketlib = require('socket')
local os = require('os')

local TARANTOOL_SRC_DIR = os.getenv("TARANTOOL_SRC_DIR") or "../.."
test:diag("TARANTOOL_SRC_DIR=%s", TARANTOOL_SRC_DIR)

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

local function start_server(test, sock_family, sock_addr)
    test:diag("starting HTTP server on %s...", sock_addr)
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
    local cmd = string.format("%s/test/app-tap/httpd.py %s",
                              TARANTOOL_SRC_DIR, arg)
    local server = io.popen(cmd)
    test:is(server:read("*l"), "heartbeat", "server started")
    test:diag("trying to connect to %s", url)
    local r
    for i=1,10 do
        r = client.get(url, merge(opts, {timeout = 0.01}))
        if r.status == 200 then
            break
        end
        fiber.sleep(0.01)
    end
    test:is(r.status, 200, "connection is ok")
    if r.status ~= 200 then
        os.exit(1)
    end
    return server, url, opts
end

local function stop_server(test, server)
    test:diag("stopping HTTP server")
    server:close()
end

local function test_http_client(test, url, opts)
    test:plan(9)

    test:isnil(rawget(_G, 'http'), "global namespace is not polluted");
    test:isnil(rawget(_G, 'http.client'), "global namespace is not polluted");
    local r = client.get(url, opts)
    test:is(r.status, 200, 'simple 200')
    test:is(r.proto[1], 1, 'proto major http 1.1')
    test:is(r.proto[2], 1, 'proto major http 1.1')
    test:ok(r.body:match("hello") ~= nil, "body")
    test:ok(tonumber(r.headers["content-length"]) > 0,
        "content-length > 0")
    test:is(client.get("http://localhost:0/").status, 595, 'bad url')

    local r = client.request('GET', url, nil, opts)
    test:is(r.status, 200, 'request')
end

local function test_cancel_and_timeout(test, url, opts)
    test:plan(2)
    local ch = fiber.channel(1)
    local http = client:new()
    local f = fiber.create(function()
                ch:put(http:get(url, opts)) end)
    f:cancel()
    local r = ch:get()
    test:ok(r.status == 408 and string.find(r.reason, "Timeout"),
                    "After cancel fiber timeout is returned")
    local r = http:get(url, merge(opts, {timeout = 0.0001}))
    test:ok(r.status == 408 and string.find(r.reason, "Timeout"),
                                                       "Timeout check")
end

local function test_post_and_get(test, url, opts)
    test:plan(19)

    local http = client.new()
    test:ok(http ~= nil, "client is created")

    local headers = { header1 = "1", header2 = "2" }
    local my_body = { key = "value" }
    local json_body = json.encode(my_body)
    local responses = {}
    local data = {a = 'b'}
    headers['Content-Type'] = 'application/json'
    local fibers = 7
    local ch = fiber.channel(fibers)
    opts = merge(opts, {headers = headers})
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
    for i=1,fibers do
        ch:get()
    end
    local r = responses.good_get
    test:is(r.status, 200, "GET: default http code page exists")
    test:is(r.body, "hello world", "GET: default right body")

    r = responses.get2
    test:is(r.status, 200, "GET: http code page exists")
    test:is(r.body, "abc", "GET: right body")

    r = responses.absent_get
    test:is(r.status, 500, "GET: absent method http code page exists")
    test:is(r.body, "No such method", "GET: absent method right body")

    r = responses.empty_post
    test:is(r.status, 200, "POST: good status")
    test:ok(r.headers['header1'] == headers.header1 and
        r.headers['header2'] == headers.header2, "POST: good headers")
    test:isnil(r.body, "POST: empty body")

    r = responses.good_post
    test:is(r.status, 200, "POST: good status")
    test:ok(r.headers['header1'] == headers.header1 and
        r.headers['header2'] == headers.header2, "POST: good headers")
    test:is(r.body, json_body, "POST: body")

    r = responses.good_put
    test:is(r.status, 200, "PUT: good status")
    test:ok(r.headers['header'] == headers.header and
        r.headers['header2'] == headers.header2, "PUT: good headers")

    r = responses.bad_get
    test:is(r.status, 404, "GET: http page not exists")
    test:isnt(r.body:len(), 0, "GET: not empty body page not exists")
    test:ok(string.find(r.body, "Not Found"),
                "GET: right body page not exists")

    local st = http:stat()
    test:ok(st.sockets_added == st.sockets_deleted and
        st.active_requests == 0,
        "stats checking")
end

local function test_errors(test)
    test:plan(3)
    local http = client:new()
    local status, err = pcall(http.get, http, "htp://mail.ru")
    test:ok(not status and string.find(json.encode(err),
                        "Unsupported protocol"),
                        "GET: exception on bad protocol")
    status, err = pcall(http.post, http, "htp://mail.ru", "")
    test:ok(not status and string.find(json.encode(err),
                        "Unsupported protocol"),
                        "POST: exception on bad protocol")
    local r = http:get("http://do_not_exist_8ffad33e0cb01e6a01a03d00089e71e5b2b7e9930dfcba.ru")
    test:is(r.status, 595, "GET: response on bad url")
end

local function test_headers(test, url, opts)
    test:plan(15)
    local http = client:new()
    local r = http:get(url .. 'headers', opts)
    test:is(type(r.headers["set-cookie"]), 'string', "set-cookie check")
    test:ok(r.headers["set-cookie"]:match("likes=cheese"), "set-cookie check")
    test:ok(r.headers["set-cookie"]:match("age = 17"), "set-cookie check")
    test:is(r.headers["content-type"], "application/json", "content-type check")
    test:is(r.headers["my_header"], "value1,value2", "other header check")
    test:is(r.cookies["likes"][1], "cheese", "cookie value check")
    test:ok(r.cookies["likes"][2][1]:match("Expires"), "cookie option check")
    test:ok(r.cookies["likes"][2][3]:match("HttpOnly"), "cookie option check")
    test:is(r.cookies["age"][1], "17", "cookie value check")
    test:is(#r.cookies["age"][2], 1, "cookie option check")
    test:is(r.cookies["age"][2][1], "Secure", "cookie option check")
    test:ok(r.cookies["good_name"] ~= nil , "cookie name check")
    test:ok(r.cookies["bad@name"] == nil , "cookie name check")
    test:ok(r.cookies["badname"] == nil , "cookie name check")
    test:ok(r.cookies["badcookie"] == nil , "cookie name check")
end

local function test_special_methods(test, url, opts)
    test:plan(14)
    local http = client.new()
    local responses = {}
    local fibers = 7
    local ch = fiber.channel(fibers)
    local _
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
    for i = 1, fibers do
        ch:get()
    end

    test:is(responses.patch_data.status, 200, "HTTP:PATCH request")
    test:ok(json.decode(responses.patch_data.body).key == "val",
        "HTTP:PATCH request content")
    test:is(responses.delete_data.status, 200, "HTTP:DELETE request")
    test:ok(responses.delete_data.headers.method == "DELETE",
        "HTTP:DELETE request content")
    test:is(responses.options_data.status, 200, "HTTP:OPTIONS request")
    test:ok(responses.options_data.headers.method == "OPTIONS",
        "HTTP:OPTIONS request content")
    test:is(responses.head_data.status, 200, "HTTP:HEAD request code")
    test:ok(responses.head_data.headers.method == "HEAD",
        "HTTP:HEAD request content")
    test:is(responses.connect_data.status, 200, "HTTP:CONNECT request")
    test:ok(responses.connect_data.headers.method == "CONNECT",
        "HTTP:OPTIONS request content")
    test:is(responses.trace_data.status, 200, "HTTP:TRACE request")
    test:ok(responses.trace_data.headers.method == "TRACE",
        "HTTP:TRACE request content")
    test:is(responses.custom_data.status, 400, "HTTP:CUSTOM request")
    test:ok(responses.custom_data.headers.method == "CUSTOM",
        "HTTP:CUSTOM request content")
end

local function test_concurrent(test, url, opts)
    test:plan(3)
    local http = client.new()
    local headers = { my_header = "1", my_header2 = "2" }
    local my_body = { key = "value" }
    local json_body = json.encode(my_body)
    local num_test = 10
    local num_load = 10
    local curls   = { }
    local headers = { }

    -- Init [[
    for i = 1, num_test do
        headers["My-header" .. i] = "my-value"
    end

    for i = 1, num_test do
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
        for j=1,num_load do
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
    local ok_timeout = true
    local ok_req = true

    -- Join test
    local rest = num_test
    while true do
        local ticks = 0
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
    test:is(ok_req, true, "All requests are ok")
    test:ok(ok_sockets_added, "free sockets")
    test:ok(ok_active, "no active requests")
end

function run_tests(test, sock_family, sock_addr)
    test:plan(9)
    local server, url, opts = start_server(test, sock_family, sock_addr)
    test:test("http.client", test_http_client, url, opts)
    test:test("cancel and timeout", test_cancel_and_timeout, url, opts)
    test:test("basic http post/get", test_post_and_get, url, opts)
    test:test("errors", test_errors)
    test:test("headers", test_headers, url, opts)
    test:test("special methods", test_special_methods, url, opts)
    if sock_family == 'AF_UNIX' and jit.os ~= "Linux" then
        --
        -- BSD-based operating systems (including OS X) will fail
        -- connect() to a Unix domain socket with ECONNREFUSED
        -- if the queue of pending connections is full. Hence the
        -- "concurrent" test, which opens a lot of connections
        -- simultaneously, cannot run on those platforms. Linux,
        -- however, is fine - instead of returning ECONNEREFUSED
        -- it will suspend connect() until backlog is processed.
        --
        test:skip("concurrent")
    else
        test:test("concurrent", test_concurrent, url, opts)
    end
    stop_server(test, server)
end

test:plan(2)

test:test("http over AF_INET", function(test)
    local s = socketlib('AF_INET', 'SOCK_STREAM', 0)
    s:bind('127.0.0.1', 0)
    local host = s:name().host
    local port = s:name().port
    s:close()
    run_tests(test, 'AF_INET', string.format("%s:%d", host, port))
end)

test:test("http over AF_UNIX", function(test)
    local path = os.tmpname()
    os.remove(path)
    local status = pcall(client.get, 'http://localhost/', {unix_socket = path})
    if not status then
        -- Unix domain sockets are not supported, skip the test.
        return
    end
    run_tests(test, 'AF_UNIX', path)
    os.remove(path)
end)

os.exit(test:check() == true and 0 or -1)
