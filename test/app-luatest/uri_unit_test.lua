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
    { params = { datetime_type = datetime.new() }, query_string = "datetime_type=1970-01-01T00%3A00%3A00Z" },
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

local uri_creds_g = t.group("uri_creds", {
    {tab = {uri = 'localhost:3301', login = 'one', password = 'two'},
     res = {login = 'one', password = 'two'}},
    {tab = {uri = 'localhost:3301', login = 'one'},
     res = {login = 'one'}},
    {tab = {uri = 'alpha:qwe@localhost:3301', login = 'one', password = 'two'},
     res = {login = 'one', password = 'two'}},
    {tab = {uri = 'alpha:qwe@localhost:3301', login = 'one'},
     res = {login = 'one'}},
})

uri_creds_g.test_creads = function(cg)
    local res = uri.parse(cg.params.tab)
    t.assert_equals(res.login, cg.params.res.login)
    t.assert_equals(res.password, cg.params.res.password)
end

local uri_creds_errors_g = t.group("uri_creds_errors", {
    {tab = {uri = 'localhost:3301', login = 1, password = 'two'},
     err = 'Invalid URI table: expected type for login is string'},
    {tab = {uri = 'localhost:3301', login = 'one', password = 2},
     err = 'Invalid URI table: expected type for password is string'},
    {tab = {uri = 'alpha:qwe@localhost:3301', password = 'two'},
     err = 'Invalid URI table: login required if password is set'},
})

uri_creds_errors_g.test_creads_errors = function(cg)
    local res, err = uri.parse(cg.params.tab)
    t.assert(res == nil)
    t.assert_equals(err.message, cg.params.err)
end

local uri_set_creds_errors_g = t.group("uri_set_creds_errors", {
    {tab = {'localhost:3301', 'localhost:3302', login = 'one'},
     err = 'URI login is not allowed for multiple URIs'},
    {tab = {'localhost:3301', 'localhost:3302', password = 'two'},
     err = 'URI password is not allowed for multiple URIs'},
})

uri_set_creds_errors_g.test_set_creds_errors = function(cg)
    local res, err = uri.parse_many(cg.params.tab)
    t.assert(res == nil)
    t.assert_equals(err.message, cg.params.err)
end

g.test_params_escaping = function(_)
    local uri_params = uri._internal.params
    local params = {
        [1] = {"ы", "d&" },
        ["k%"] = "d%"
    }
    t.assert_equals(uri_params(params, uri.RFC3986),
                    "1=%D1%8B&1=d%26&k%25=d%25")
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

g.test_encode_kv_escaping = function(_)
    local encode_kv = uri._internal.encode_kv
    local res = {}
    encode_kv("т", { "б", "д" }, res, uri.RFC3986)
    t.assert_items_equals(res, { "%D1%82=%D0%B1", "%D1%82=%D0%B4" })
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

g.test_uri_escape_plus_enabled = function(_)
    local opts = {
        plus = true,
    }
    t.assert_equals(uri.escape(" ", opts), "+")
end

g.test_uri_escape_plus_disabled = function(_)
    local opts = {
        plus = false,
    }
    t.assert_equals(uri.escape(" ", opts), "%20")
end

g.test_uri_escape_reserved = function(_)
    local opts = {
        unreserved = uri.unreserved("B"),
    }
    t.assert_equals(uri.escape("B", opts), "B")
end

g.test_uri_escape_unreserved = function(_)
    local opts = {
        unreserved = uri.unreserved("A"), -- Non-default unreserved symbols.
    }
    t.assert_equals(uri.escape("B", opts), "%42")
end

g.test_uri_unescape = function(_)
    t.assert_equals(uri.unescape("%42"), "B")
end

g.test_uri_unescape_plus_enabled = function(_)
    local opts = {
        plus = true
    }
    t.assert_equals(uri.unescape("+", opts), " ")
end

g.test_uri_unescape_plus_disabled = function(_)
    local opts = {
        plus = false
    }
    t.assert_equals(uri.unescape("+", opts), "+")
end
