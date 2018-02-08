#!/usr/bin/env tarantool

local tap = require('tap')
local client = require('http.client')
local json = require('json')
local test = tap.test("curl")
local fiber = require('fiber')
local socketlib = require('socket')
local os = require('os')
local yaml = require('yaml')

local TARANTOOL_SRC_DIR = os.getenv("TARANTOOL_SRC_DIR") or "../.."
test:diag("TARANTOOL_SRC_DIR="..TARANTOOL_SRC_DIR)

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

local function http_client_test(test, URL, opts)
    -- "http.client"
    test:plan(9)

    test:isnil(rawget(_G, 'http'), "global namespace is not polluted");
    test:isnil(rawget(_G, 'http.client'), "global namespace is not polluted");
    local r = client.get(URL, opts)
    test:is(r.status, 200, 'simple 200')
    test:is(r.proto[1], 1, 'proto major http 1.1')
    test:is(r.proto[2], 1, 'proto major http 1.1')
    test:ok(r.body:match("hello") ~= nil, "body")
    test:ok(tonumber(r.headers["content-length"]) > 0,
        "content-length > 0")
    test:is(client.get("http://localhost:0/").status, 595, 'bad url')

    local r = client.request('GET', URL, nil, opts)
    test:is(r.status, 200, 'request')
end

local function cancel_and_timeout_test(test, URL, opts)
    -- "cancel and timeout"
    test:plan(2)
    local ch = fiber.channel(1)
    local http = client:new()
    local f = fiber.create(function()
                ch:put(http:get(URL, opts)) end)
    f:cancel()
    local r = ch:get()
    test:ok(r.status == 408 and string.find(r.reason, "Timeout"),
                    "After cancel fiber timeout is returned")
    local r = http:get(URL, merge(opts, {timeout = 0.0001}))
    test:ok(r.status == 408 and string.find(r.reason, "Timeout"),
                                                       "Timeout check")
end

local function post_and_get_test(test, URL, opts)
    -- "basic http post/get"
    test:plan(19)

    local http = client.new()
    test:ok(http ~= nil, "client is created")

    local headers = { header1 = "1", header2 = "2" }
    local options = merge(opts, {headers = headers})
    local my_body = { key = "value" }
    local json_body = json.encode(my_body)
    local responses = {}
    local data = {a = 'b'}
    headers['Content-Type'] = 'application/json'
    local fibers = 7
    local ch = fiber.channel(fibers)
    local _
    fiber.create(function()
        responses.good_get = http:get(URL, options)
        ch:put(1)
    end)
    fiber.create(function()
        responses.get2 = http:get(URL.."abc", options)
        ch:put(1)
    end)
    fiber.create(function()
        responses.good_post = http:post(URL, json_body, options)
        ch:put(1)
    end)
    fiber.create(function()
        responses.empty_post = http:post(URL, nil, options)
        ch:put(1)
    end)
    fiber.create(function()
        responses.good_put = http:put(URL, json_body, options)
        ch:put(1)
    end)
    fiber.create(function()
        responses.bad_get = http:get(URL .. 'this/page/not/exists',
            options)
        ch:put(1)
    end)
    fiber.create(function()
        responses.absent_get = http:get(URL .. 'absent',
            options)
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

local function errors_test(test)
    -- "errors"
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

local function headers_test(test, URL, opts)
    -- "headers"
    test:plan(4)
    local http = client:new()
    local r = http:get(URL .. 'headers', opts)
    test:is(type(r.headers["set-cookie"]), 'string', "set-cookie check")
    test:is(r.headers["set-cookie"], "likes=cheese,age=17", "set-cookie check")
    test:is(r.headers["content-type"], "application/json", "content-type check")
    test:is(r.headers["my_header"], "value1,value2", "other header check")
end

local function special_methods_test(test, URL, opts)
    -- "special methods"
    test:plan(14)
    local http = client.new()
    local responses = {}
    local fibers = 7
    local ch = fiber.channel(fibers)
    local _
    fiber.create(function()
        responses.patch_data = http:patch(URL, "{\"key\":\"val\"}", opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.delete_data = http:delete(URL, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.options_data = http:options(URL, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.head_data = http:head(URL, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.trace_data = http:trace(URL, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.connect_data = http:connect(URL, opts)
        ch:put(1)
    end)
    fiber.create(function()
        responses.custom_data = http:request("CUSTOM", URL, nil, opts)
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

local function concurrent_test(test, URL, opts)
    -- "concurrent"
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
            url = URL,
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

test:plan(3)

test:test("errors", errors_test)

test:test('http over AF_INET', function(test)
    test:plan(8)

    local s = socketlib('AF_INET', 'SOCK_STREAM', 0)
    s:bind('127.0.0.1', 0)
    local host = s:name().host
    local port = s:name().port
    s:close()

    test:diag("starting HTTP server on %s:%s...", host, port)
    local cmd = string.format("%s/test/app-tap/httpd.py --inet %s %s",
            TARANTOOL_SRC_DIR, host, port)
    local server = io.popen(cmd)
    test:is(server:read("*l"), "heartbeat", "server started")
    
    local url = string.format("http://%s:%s/", host, port)
    test:diag("trying to connect to %s", url)
    local r
    for i=1,10 do
        r = client.get(url, {timeout = 0.01})
        if r.status == 200 then
            break
        end
        fiber.sleep(0.01)
    end
    
    test:is(r.status, 200, "connection is ok")
    if r.status ~= 200 then
        server:close()
        return
    end

    -- RUN TESTS
    test:test("http.client", http_client_test, url, {})
    test:test("cancel and timeout", cancel_and_timeout_test, url, {})
    test:test("basic http post/get", post_and_get_test, url, {})
    test:test("headers", headers_test, url, {})
    test:test("special methods", special_methods_test, url, {})
    test:test("concurrent", concurrent_test, url, {})

    -- STOP SERVER
    test:diag("stopping HTTP server")
    server:close()
end)

test:test('http over AF_UNIX', function(test)
    local url = "http://localhost/"
    local tmpname = os.tmpname()
    local sock = tmpname .. '.sock'
    local ok = pcall(client.get, url, {unix_socket=sock, timeout = 0.01})
    if ok == false then
        os.remove(tmpname)
        return
    end

    test:plan(8)

    test:diag("starting HTTP over UNIX socket '%s...'", sock)
    
    local cmd = string.format("%s/test/app-tap/httpd.py --unix %s",
            TARANTOOL_SRC_DIR, sock)
    local server = io.popen(cmd)
    if server == nil then
        test:diag("create server failes")
        os.remove(tmpname)
        os.remove(sock)
        return
    end
    test:is(server:read("*l"), "heartbeat", "server started")

    test:diag("trying to connect to %s", url)
    local r
    for i=1,10 do
        r = client.get(url, {unix_socket=sock, timeout = 0.01})
        if r.status == 200 then
            break
        end
        fiber.sleep(0.01)
    end
    
    test:is(r.status, 200, "connection is ok")
    if r.status ~= 200 then
        os.remove(tmpname)
        os.remove(sock)
        server:close()
        return
    end

    -- RUN TESTS
    local opts = {unix_socket = sock}
    test:test("http.client", http_client_test, url, opts)
    test:test("cancel and timeout", cancel_and_timeout_test, url, opts)
    test:test("basic http post/get", post_and_get_test, url, opts)
    test:test("headers", headers_test, url, opts)
    test:test("special methods", special_methods_test, url, opts)
    if jit.os == "OSX" then -- due to difference in OSX Unix socket, skip
        test:skip("concurrent")
    else
        test:test("concurrent", concurrent_test, url, opts)
    end

    -- STOP SERVER
    test:diag("stopping HTTP server")
    os.remove(tmpname)
    os.remove(sock)
    server:close()
end)

os.exit(test:check() == true and 0 or -1)
