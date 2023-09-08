local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'gh-6766'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

g.test_6766_1 = function()
    local conn = g.server.net_box
    local val = require('uuid').new()
    local res = {{'uuid'}}
    local rows = conn:execute([[SELECT TYPEOF(?);]], {val}).rows
    t.assert_equals(rows, res)
end

g.test_6766_2 = function()
    local conn = g.server.net_box
    local val = require('decimal').new(1.5)
    local res = {{'decimal'}}
    local rows = conn:execute([[SELECT TYPEOF(?);]], {val}).rows
    t.assert_equals(rows, res)
end

g.test_6766_3 = function()
    local conn = g.server.net_box
    local val = require('datetime').now()
    local res = {{'datetime'}}
    local rows = conn:execute([[SELECT TYPEOF(?);]], {val}).rows
    t.assert_equals(rows, res)
end

g.test_6766_4 = function()
    local conn = g.server.net_box
    local val = require('msgpack').object_from_raw('\xc7\x00\x0f')
    local res = "Bind value type USERDATA for parameter 1 is not supported"
    local _, err = pcall(conn.execute, conn, [[SELECT TYPEOF(?);]], {val})
    t.assert_equals(err.message, res)
end
