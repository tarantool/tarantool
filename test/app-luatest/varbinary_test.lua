local compat = require('compat')
local ffi = require('ffi')
local json = require('json')
local msgpack = require('msgpack')
local msgpackffi = require('msgpackffi')
local varbinary = require('varbinary')
local yaml = require('yaml')

local t = require('luatest')
local g = t.group()

g.test_new_invalid_args = function()
    local errmsg = 'Usage: varbinary.new(str) or varbinary.new(ptr, size)'
    t.assert_error_msg_equals(errmsg, varbinary.new, 1)
    t.assert_error_msg_equals(errmsg, varbinary.new, {})
    t.assert_error_msg_equals(errmsg, varbinary.new, true)
    t.assert_error_msg_equals(errmsg, varbinary.new,
                              ffi.cast('const char *', 'foo'))
    t.assert_error_msg_equals(errmsg, varbinary.new,
                              ffi.cast('const char *', 'foo'), 'bar')
end

g.test_new_from_nil = function()
    local v = varbinary.new()
    t.assert(varbinary.is(v))
    t.assert_equals(#v, 0)
    t.assert_equals(tostring(v), '')
    t.assert_equals(v, '')
    t.assert_equals('', v)
    t.assert_equals(v, v)
    t.assert_equals(v, varbinary.new())
    t.assert_equals(v, varbinary.new(''))
end

g.test_new_from_str = function()
    local v = varbinary.new('foo')
    t.assert(varbinary.is(v))
    t.assert_equals(#v, 3)
    t.assert_equals(tostring(v), 'foo')
    t.assert_equals(v, 'foo')
    t.assert_equals('foo', v)
    t.assert_equals(v, v)
    t.assert_equals(v, varbinary.new('foo'))
    t.assert_equals(v, varbinary.new(ffi.cast('const char *', 'foo'), 3))
end

g.test_new_from_ptr = function()
    local v = varbinary.new(ffi.cast('const char *', 'foo'), 3)
    t.assert(varbinary.is(v))
    t.assert_equals(#v, 3)
    t.assert_equals(tostring(v), 'foo')
    t.assert_equals(v, 'foo')
    t.assert_equals('foo', v)
    t.assert_equals(v, v)
    t.assert_equals(v, varbinary.new('foo'))
    t.assert_equals(v, varbinary.new(ffi.cast('const char *', 'foo'), 3))
end

g.test_is = function()
    t.assert_equals(varbinary.is(varbinary.new()), true)
    t.assert_equals(varbinary.is(varbinary.new('')), true)
    t.assert_equals(varbinary.is(varbinary.new('foo')), true)
    t.assert_equals(varbinary.is(nil), false)
    t.assert_equals(varbinary.is(msgpack.NULL), false)
    t.assert_equals(varbinary.is(1), false)
    t.assert_equals(varbinary.is({}), false)
    t.assert_equals(varbinary.is(''), false)
    t.assert_equals(varbinary.is('foo'), false)
end

g.test_len = function()
    t.assert_equals(#varbinary.new(), 0)
    t.assert_equals(#varbinary.new(''), 0)
    t.assert_equals(#varbinary.new('foo'), 3)
    t.assert_equals(#varbinary.new(ffi.cast('const char *', 'foobar'), 6), 6)
end

g.test_tostring = function()
    t.assert_equals(tostring(varbinary.new()), '')
    t.assert_equals(tostring(varbinary.new('')), '')
    t.assert_equals(tostring(varbinary.new('foo')), 'foo')
    t.assert_equals(tostring(varbinary.new(
        ffi.cast('const char *', 'foobar'), 6)), 'foobar')
end

g.test_eq = function()
    local v1 = varbinary.new('foo')
    local v2 = varbinary.new(ffi.cast('const char *', 'foo'), 3)
    local v3 = varbinary.new('foobar')
    t.assert_equals('foo', v1)
    t.assert_equals('foo', v2)
    t.assert_equals('foobar', v3)
    t.assert_not_equals('foobar', v1)
    t.assert_not_equals('foobar', v2)
    t.assert_not_equals('foo', v3)
    t.assert_equals(v1, 'foo')
    t.assert_not_equals(v1, 'foobar')
    t.assert_equals(v1, v1)
    t.assert_equals(v1, v2)
    t.assert_not_equals(v1, v3)
    t.assert_equals(v2, 'foo')
    t.assert_not_equals(v2, 'foobar')
    t.assert_equals(v2, v1)
    t.assert_equals(v2, v2)
    t.assert_not_equals(v2, v3)
    t.assert_equals(v3, 'foobar')
    t.assert_not_equals(v3, 'foo')
    t.assert_not_equals(v3, v1)
    t.assert_not_equals(v3, v2)
    t.assert_equals(v3, v3)
end

-- Map: string => expected base64 encoding.
local base64_tests = {
    {'', ''},
    {'\xFF', '/w=='},
    {'foo', 'Zm9v'},
    {
        string.rep('x', 100),
        string.rep('eHh4', 33) .. 'eA=='
    },
}

g.test_yaml = function()
    for _, i in ipairs(base64_tests) do
        local v = varbinary.new(i[1])
        local r = '--- !!binary ' .. i[2] .. '\n...\n'
        local v2 = yaml.decode(r)
        local r2 = yaml.encode(v)
        t.assert_equals(r2, r)
        t.assert_equals(v2, v)
        t.assert(varbinary.is(v2))
    end
end

g.test_tuple_tostring = function()
    for _, i in ipairs(base64_tests) do
        local v = varbinary.new(i[1])
        local r = '[!!binary ' .. (i[2] == '' and "''" or i[2]) .. ']'
        t.assert_equals(tostring(box.tuple.new(v)), r)
    end
end

-- Map: string => expected msgpack encoding.
local msgpack_tests = {
    {'', '\xC4\x00'},
    {'\xFF', '\xC4\x01\xFF'},
    {'foo', '\xC4\x03foo'},
    {string.rep('x', 300), '\xC5\x01\x2C' .. string.rep('x', 300)},
    {
        string.rep('x', 70000),
        '\xC6\x00\x01\x11\x70' .. string.rep('x', 70000)
    },
}

g.test_msgpack = function()
    for _, i in ipairs(msgpack_tests) do
        local v = varbinary.new(i[1])
        local r = i[2]
        local v2 = msgpack.decode(r)
        local r2 = msgpack.encode(v)
        t.assert_equals(r2, r)
        t.assert_equals(v2, v)
        t.assert(varbinary.is(v2))
    end
end

g.test_msgpackffi = function()
    for _, i in ipairs(msgpack_tests) do
        local v = varbinary.new(i[1])
        local r = i[2]
        local v2 = msgpackffi.decode(r)
        local r2 = msgpackffi.encode(v)
        t.assert_equals(r2, r)
        t.assert_equals(v2, v)
        t.assert(varbinary.is(v2))
    end
end

-- JSON encoder converts binary data to string.
g.test_json = function()
    t.assert_equals(json.encode(varbinary.new()), "\"\"")
    t.assert_equals(json.encode(varbinary.new('foo')), "\"foo\"")
    t.assert_equals(json.encode(varbinary.new('\xFF')), "\"\xFF\"")
end

-- Lua console converts binary data to string.
g.test_lua_console = function()
    local function format(v)
        return require('console.lib').format_lua({block = false, indent = 2}, v)
    end
    t.assert_equals(format(varbinary.new()), "\"\"")
    t.assert_equals(format(varbinary.new('foo')), "\"foo\"")
    t.assert_equals(format(varbinary.new('\xFF')), "\"\xFF\"")
end

g.after_test('test_compat', function()
    compat.binary_data_decoding = 'default'
end)

g.test_compat = function()
    t.assert_equals(compat.binary_data_decoding.current, 'default')
    t.assert_equals(compat.binary_data_decoding.default, 'new')
    local v = varbinary.new()
    t.assert(varbinary.is(yaml.decode(yaml.encode(v))))
    t.assert(varbinary.is(msgpack.decode(msgpack.encode(v))))
    t.assert(varbinary.is(msgpackffi.decode(msgpackffi.encode(v))))
    compat.binary_data_decoding = 'old'
    t.assert_equals(type(yaml.decode(yaml.encode(v))), 'string')
    t.assert_equals(type(msgpack.decode(msgpack.encode(v))), 'string')
    t.assert_equals(type(msgpackffi.decode(msgpackffi.encode(v))), 'string')
end
