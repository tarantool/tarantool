local t = require('luatest')
local protobuf = require('protobuf')
local g = t.group()

g.test_ordinary_string = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'string', 1}})
    })
    local result = protocol:encode('test', {val = 'protobuf'})
    t.assert_equals(string.hex(result), '0a0870726f746f627566')
end

g.test_nested_messages = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {exv = {'nest', 1}}),
        protobuf.message('nest', {inv = {'string', 1}})
    })
    local data = {exv = {inv = 'protobuf'}}
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), '0a0a0a0870726f746f627566')
end
g.test_byte = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'bytes', 1}})
    })
    local data = {val = '0a0a0a0870726f746f627566'}
    local proto_res = '0a18306130613061303837303732366637343666363237353636'
    local result = protocol:encode('test', data)
    t.assert_equals(string.hex(result), proto_res)
end

g.test_very_long_string = function()
    local protocol = protobuf.protocol({
        protobuf.message('test', {val = {'string', 1}})
    })
    local result = protocol:encode('test', {val = ('a'):rep(2^15)})
    t.assert_equals(string.hex(result), '0a808002' .. ('61'):rep(2^15))
end
