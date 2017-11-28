#!/usr/bin/env tarantool

-- Testing feedback module

local tap = require('tap')
local json = require('json')
local fiber = require('fiber')
local test = tap.test('feedback_daemon')
local socket = require('socket')

test:plan(10)

box.cfg{log = 'report.log', log_level = 6}

local function http_handle(s)
    s:write("HTTP/1.1 200 OK\r\n")
    s:write("Accept: */*\r\n")
    s:write("Connection: keep-alive\r\n")
    s:write("Access-Control-Allow-Origin: *\r\n")
    s:write("Access-Control-Allow-Credentials: true\r\n")
    s:write("Content-Length: 0\r\n\r\n")

    local buf = s:read('\n')
    local length
    while not buf:match("^\r\n") do
        if buf:lower():match("content%-length") then
            length = tonumber(buf:split(": ")[2])
        end
        buf = s:read('\n')
    end
    buf = s:read(length)
    local ok, data = pcall(json.decode, buf)
    if ok then
        box.space._schema:put({'feedback', buf })
    end
end

local server = socket.tcp_server("127.0.0.1", 0, http_handle)
local port = server:name().port

local interval = 0.01
box.cfg{
    feedback_host = string.format("127.0.0.1:%i", port),
    feedback_interval = interval,
}

local function check(message)
    while box.space._schema:get('feedback') == nil do fiber.sleep(0.001) end
    local data = box.space._schema:get('feedback')
    test:ok(data ~= nil, message)
    box.space._schema:delete('feedback')
end

check("initial check")
local daemon = box.internal.feedback_daemon
-- check if feedback has been sent and received
daemon.reload()
check("feedback received after reload")

local errinj = box.error.injection
errinj.set("ERRINJ_HTTPC", true)
check('feedback received after errinj')
errinj.set("ERRINJ_HTTPC", false)

daemon.send_test()
check("feedback received after explicit sending")

box.cfg{feedback_enabled = false}
daemon.send_test()
while box.space._schema:get('feedback') ~= nil do fiber.sleep(0.001) end
test:ok(box.space._schema:get('feedback') == nil, "no feedback after disabling")

box.cfg{feedback_enabled = true}
daemon.send_test()
check("feedback after start")

daemon.stop()
daemon.send_test()
while box.space._schema:get('feedback') ~= nil do fiber.sleep(0.001) end
test:ok(box.space._schema:get('feedback') == nil, "no feedback after stop")

daemon.start()
daemon.send_test()
check("feedback after start")

box.feedback.save("feedback.json")
daemon.send_test()
while box.space._schema:get('feedback') == nil do fiber.sleep(0.001) end
local data = box.space._schema:get('feedback')
local fio = require("fio")
local fh = fio.open("feedback.json")
test:ok(fh, "file is created")
local file_data = fh:read()
test:is(file_data, data[2], "data is equal")
fh:close()
fio.unlink("feedback.json")

server:close()
-- check it does not fail without server
local daemon = box.internal.feedback_daemon
daemon.start()
daemon.send_test()

test:check()
os.exit(0)
