local t = require("luatest")
local json = require("json")
local msgpack = require("msgpack")
local yaml = require("yaml")

local decimal = require("decimal")
local datetime = require("datetime")
local uuid = require("uuid")
local ffi = require("ffi")

local g = t.group('http_client_unit')

package.loaded.http = {
    client = {
        new = function() return end
    }
}
local driver = package.loaded.http.client -- luacheck: no unused

local httpc = require("src.lua.httpc")
local encode_body = httpc._internal.encode_body
local extract_mime_type = httpc._internal.extract_mime_type
local get_icase = httpc._internal.get_icase
local default_content_type = httpc._internal.default_content_type

local encoders = {
    ['application/json'] = function(body, _content_type) return json.encode(body) end,
    ['application/yaml'] = function(body, _content_type) return yaml.encode(body) end,
    ['application/msgpack'] = function(body, _content_type) return msgpack.encode(body) end,
}

ffi.cdef[[
    typedef struct {
        double x;
        double y;
    } point_t;
]]

local id = uuid.new()
local cdata_obj = ffi.new('point_t')
local userdata_obj = newproxy(true)

local content_type_testcases = {
    { "application/x-www-form-urlencoded", "application/x-www-form-urlencoded" },
    { "text/html; charset=UTF-8", "text/html" },
    { "multipart/form-data; boundary=something", "multipart/form-data" },
    { "text/html; charset=ISO-8859-4", "text/html" },
    { "AAA ; ZZZ", "AAA" },
    { "  AAA ; ZZZ", "AAA" },
    { "     AAA ; ZZZ", "AAA" },
    { "AAA BBB; ZZZ", "AAA BBB" },
    { nil, nil },
}

local body_encode_testcases = {
    -- Fields: body, content_type, raw_body.
    { nil, nil, "" },
    { box.NULL, default_content_type, "" },
    { nil, "application/json", "" },
    { box.NULL, "application/json", "" },
    { { a = 1 }, "application/json", json.encode({ a = 1 }) },
    { { a = 1 }, "application/msgpack", msgpack.encode({ a = 1 }) },
    { { a = 1 }, "application/yaml", yaml.encode({ a = 1 }) },
    { { a = 1 }, "application/json; charset=utf-8", json.encode({ a = 1 }) },
    { { c = 4 }, "application/json  ; charset=utf-8", json.encode({ c = 4 }) },
    { true, nil, tostring(true) },
    { "abc", nil, "abc" },
    { 1993, nil, "1993" },
    { cdata_obj, "application/tarantool", "content_type is application/tarantool, body type is cdata" },
    { userdata_obj, "application/tarantool", "content_type is application/tarantool, body type is userdata" },
    { { b = 2 }, "application/tarantool", "content_type is application/tarantool, body type is table" },
    { decimal.new("0.1"), default_content_type, json.encode(decimal.new("0.1")) },
    { datetime.new(), default_content_type, json.encode(datetime.new()) },
    { id, default_content_type, json.encode(id) },
}

g.before_test("test_unit_encode_body", function()
    encoders["application/tarantool"] = function(body, content_type)
        return ("content_type is %s, body type is %s"):format(content_type, type(body))
    end
end)

g.after_test("test_unit_encode_body", function()
    encoders["application/tarantool"] = nil
end)

g.test_unit_encode_body = function(_)
    for _, tc in pairs(body_encode_testcases) do
        local body = tc[1]
        local content_type = tc[2]
        local raw_body = tc[3]
        local res = encode_body(body, content_type, encoders)
        t.assert_equals(res, raw_body)
    end
end

g.before_test("test_unit_encode_body_custom_encoder", function()
    local content_type = "application/tarantool"
    encoders[content_type] = function(body, _content_type)
        local mutated_body = body
        mutated_body.b = 5 -- Set an additional key.
        return json.encode(mutated_body)
    end
    g.content_type = content_type
end)

g.after_test("test_unit_encode_body_custom_encoder", function()
    local content_type = g.content_type
    encoders[content_type] = nil
end)

g.test_unit_encode_body_custom_encoder = function(cg)
    local body = { a = 1 }
    local content_type = cg.content_type
    local ok, res = pcall(encode_body, body, content_type, encoders)
    t.assert_equals(ok, true)
    t.assert_equals(res, json.encode({a = 1, b = 5}))
end

g.test_unit_encode_body_unknown_encoder = function(_)
    local body = { a = 1 }
    local ok, res = pcall(encode_body, body, "application/unknown", encoders)
    t.assert_equals(ok, false)
    t.assert_str_contains(res, "Unable to encode body: encode function is not found (application/unknown)")
end

g.test_unit_encode_body_default_encoder = function(_)
    local body = { a = 1 }
    local content_type = default_content_type
    local res = encode_body(body, content_type, encoders)
    local encoder = encoders[content_type]
    t.assert_equals(res, encoder(body, content_type))
end

g.before_test("test_unit_encode_body_broken_encoder", function()
    local content_type = "application/broken"
    encoders[content_type] = function(_body, _content_type)
        error("Function is broken", 2)
    end
    g.content_type = content_type
end)

g.after_test("test_unit_encode_body_broken_encoder", function()
    local content_type = g.content_type
    encoders[content_type] = nil
end)

g.test_unit_encode_body_broken_encoder = function(cg)
    local body = {}
    local content_type = cg.content_type
    local ok, err = pcall(encode_body, body, content_type, encoders)
    t.assert_equals(ok, false)
    t.assert_str_contains(err, "Unable to encode body: Function is broken")
end

g.test_unit_extract_mime_type = function(_)
    for _, tc in pairs(content_type_testcases) do
        local content_type = tc[1]
        local mime_type = tc[2]
        t.assert_equals(extract_mime_type(content_type), mime_type)
    end
end

g.test_unit_get_icase = function(_)
    local tbl = {
        [2] = 3,
        ['tweedledee'] = 4,
        ['Tweedledee'] = 6,
        ['C'] = 'M',
        ['c'] = 'D',
        [true] = 'True',
    }
    t.assert_equals(get_icase(tbl, 'tweedledee'), 4)
    t.assert_equals(get_icase(tbl, 'Tweedledee'), 4)
    t.assert_equals(get_icase(tbl, 2), 3)
    t.assert_equals(get_icase(tbl, true), 'true')
    t.assert_equals(get_icase(tbl, 'XXXX'), nil)
end
