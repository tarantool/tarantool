#!/usr/bin/env tarantool

-- Testing reporting module

local tap = require('tap')
local json = require('json')
local fiber = require('fiber')
local test = tap.test('reports')

test:plan(2)

box.cfg{log = 'report.log', log_level = 6}

local function self_decorator(self)
    return function(handler)
        return function(req) return handler(self, req) end
    end
end

-- set up mock for report server
local function get_report(self, req)
    local body = req:read()
    local ok, data = pcall(json.decode, body)
    if ok then
        self:put({ 'report' })
    end
end

local httpd = require('http.server').new('0.0.0.0', '4444')
httpd:route(
    { path = '/report', method = 'POST' },
    self_decorator(box.space._schema)(get_report)
)
httpd:start()

box.cfg{
    report_host = '0.0.0.0:4444/report',
    report_interval = 1,
}

-- test report collection
local daemon = box.internal.report_daemon
local ok, report = pcall(daemon.generate_test_report)
test:is(report.server_id, box.info.uuid, 'report is filled')

-- check if report has been sent & recieved
daemon.reload()
fiber.sleep(2)
local data = box.space._schema:select('report')[1]
test:is(data ~= nil, true, 'report recieved')

test:check()
os.exit(0)
