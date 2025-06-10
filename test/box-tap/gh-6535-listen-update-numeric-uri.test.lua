#!/usr/bin/env tarantool

local tap = require('tap')
local net_box = require('net.box')
local os = require('os')

local function uris_match(uris, pattern)
    if type(uris) == "string" then
        uris = {uris}
    end
    for _, uri in ipairs(uris) do
        if uri:match(pattern) then
            return true
        end
    end
    return false
end

local function check_connection(uris)
    if type(uris) == "string" then
        uris = {uris}
    end
    for _, uri in ipairs(uris) do
        local conn = net_box.connect(uri)
        local rc = conn:ping()
        conn:close()
        if not rc then
            return false
        end
    end
    return true
end

local test = tap.test('gh-6535-listen-update-numeric-uri')
test:plan(2)
box.cfg{listen = "unix/:./tarantoolA"}
box.cfg{listen = 0}
test:ok(not uris_match(box.info.listen, "unix"), "box.info.listen")
test:ok(check_connection(box.info.listen), "conn:ping")
box.cfg{listen = ""}

os.exit(test:check() and 0 or 1)
