local t = require('luatest')

local g = t.group()

local cases = {
    {
        name = 'msgpack',
        module = require('msgpack'),
        array_data = function(s) return s.encode({1, 2, 3}) end,
        map_data = function(s) return s.encode({key = 'value'}) end,
    },
    {
        name = 'msgpackffi',
        module = require('msgpackffi'),
        array_data = function(s) return s.encode({1, 2, 3}) end,
        map_data = function(s) return s.encode({key = 'value'}) end,
    },
    {
        name = 'json',
        module = require('json'),
        array_data = function() return '[1,2,3]' end,
        map_data = function() return '{"key":"value"}' end,
    },
    {
        name = 'yaml',
        module = require('yaml'),
        array_data = function() return '---\n- 1\n- 2\n- 3\n...\n' end,
        map_data = function() return '---\nkey: value\n...\n' end,
    },
}

local function check_decoder_metatable(case, kind, serialize, bad_serialize)
    local serializer = case.module
    local data = case[kind .. '_data'](serializer)
    local decoded = serializer.decode(data)
    local mt = getmetatable(decoded)
    t.assert_equals(mt.__serialize, serialize)
    t.assert_error_msg_contains('protected decoder metatable', function()
        mt.__serialize = bad_serialize
    end)
    t.assert_equals(getmetatable(serializer.decode(data)).__serialize,
                                         serialize)

    local public_mt = serializer[kind .. '_mt']
    t.assert_equals(public_mt.__serialize, serialize)
    t.assert_error_msg_contains('protected decoder metatable', function()
        public_mt.__serialize = bad_serialize
    end)
    t.assert_equals(getmetatable(serializer.decode(data)).__serialize,
                                         serialize)
end

g.test_decoder_metatables_are_protected = function()
    for _, case in ipairs(cases) do
        check_decoder_metatable(case, 'array', 'seq', 'map')
        check_decoder_metatable(case, 'map', 'map', 'seq')
    end
end
