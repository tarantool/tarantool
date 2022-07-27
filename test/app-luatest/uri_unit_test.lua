local build_path = os.getenv("BUILDDIR")
package.cpath = build_path .. '/src/lua/?.lua;' .. package.cpath
local uri = require('src.lua.uri')
local t = require('luatest')

local decimal = require('decimal')
local datetime = require('datetime')

local g = t.group('uri_unit')

local uri_params_g = t.group("uri_params", {
    { params = { }, query_string = "" },
    { params = nil, query_string = "" },
    { params = { key = {} }, query_string = "key" },
    { params = { key = "" }, query_string = "key=" },
    { params = { db1 = "tarantool",  db2 = "redis" }, query_string = "db1=tarantool&db2=redis" },
    { params = { key = "tarantool" }, query_string = "key=tarantool" },
    { params = { boolean_type = false }, query_string = "boolean_type=false" },
    { params = { boolean_type = true }, query_string = "boolean_type=true" },
    { params = { integer_type = 50 }, query_string = "integer_type=50" },
    { params = { decimal_type = decimal.new(10) }, query_string = "decimal_type=10" },
    { params = { datetime_type = datetime.new() }, query_string = "datetime_type=1970-01-01T00:00:00Z" },
    { params = { int64_type = tonumber64(-1LL) }, query_string = "int64_type=-1LL" },
    { params = { key = {""} }, query_string = "key=" },
    { params = { key = "test" }, query_string = "key=test" },
    { params = { key = {"test"} }, query_string = "key=test" },
    { params = { key = {"ping", "pong"} }, query_string = "key=ping&key=pong" },
})

uri_params_g.test_params = function(cg)
    local uri_params = uri._internal.params
    t.assert_equals(uri_params(cg.params.params), cg.params.query_string)
end

local uri_encode_kv_g = t.group("uri_encode_kv", {
    { key = "a", values = { }, res = {"a"} },
    { key = "a", values = "b", res = {"a=b"} },
    { key = "a", values = { "b", "c" }, res = {"a=b", "a=c"} },
})

uri_encode_kv_g.test_encode_kv = function(cg)
    local encode_kv = uri._internal.encode_kv
    local res = {}
    encode_kv(cg.params.key, cg.params.values, res)
    t.assert_items_equals(res, cg.params.res)
end

local uri_values_g = t.group("uri_values", {
    { values = nil, encoded = {} },
    { values = "test", encoded = {"test"} },
    { values = "", encoded = {""} },
    { values = "a", encoded = {"a"} },
})

uri_values_g.test_values = function(cg)
    local values = uri.values(cg.params.values)
    t.assert_equals(values, cg.params.encoded)
end

g.test_uri_values_multi_arg = function(_)
    local values = uri.values("twedledee", "tweedledum")
    t.assert_items_equals(values, {"twedledee", "tweedledum"})
end

g.test_uri_values_nil = function(_)
    local values = uri.values()
    t.assert_items_equals(values, {})
end
