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

g.test_display_of_uri_params = function(_)
    local base_uri_table = { host = "localhost", service = "3301"}
    local formatted_uri_without_params = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_without_params, "localhost:3301")

    base_uri_table.params = {}
    local formatted_uri_with_empty_params = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_with_empty_params, "localhost:3301")

    base_uri_table.params = {first_param = "value1"}
    local formatted_uri_with_one_param = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_with_one_param,
                    "localhost:3301?first_param=value1")

    base_uri_table.params = {first_param = "value1", second_param = "value2"}
    local formatted_uri_with_several_params = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_with_several_params,
                    "localhost:3301?first_param=value1&second_param=value2")

    base_uri_table.params = {a = box.NULL}
    local formatted_uri_with_null_param = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_with_null_param,
                    "localhost:3301?a")

    base_uri_table.params = {param = {"value1", "value2"}}
    local formatted_uri_with_table_param = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_with_table_param,
                    "localhost:3301?param=value1&param=value2")
end

g.test_display_of_sensitive_uri_data = function(_)
    local base_uri_table = {login = "user", host = "localhost",
                            service = "3301"}
    local ssl_params = {transport = "ssl", ssl_password = "ssl_P4ssw0rd",
                        ssl_key_file = "KEY_FILE", ssl_cert_file = "CERT_FILE",
                        ssl_ca_file = "CA_FILE"}

    base_uri_table.password = "P4ssw0rd"
    local formatted_uri_with_password = uri.format(base_uri_table, true)
    local formatted_uri_without_password = uri.format(base_uri_table)
    t.assert_equals(formatted_uri_with_password,
                    "user:P4ssw0rd@localhost:3301")
    t.assert_equals(formatted_uri_without_password, "user@localhost:3301")

    base_uri_table.password = nil
    base_uri_table.params = table.deepcopy(ssl_params)
    base_uri_table.params.not_sensitive_ssl_param = "value"
    local formatted_uri_with_ssl_data = uri.format(base_uri_table, true)
    local formatted_uri_without_ssl_data = uri.format(base_uri_table)
    for ssl_key, ssl_value in pairs(ssl_params) do
        t.assert(string.match(formatted_uri_with_ssl_data, ssl_key))
        t.assert(string.match(formatted_uri_with_ssl_data, ssl_value))
    end
    t.assert_equals(formatted_uri_without_ssl_data,
                    "user@localhost:3301?transport=ssl&" ..
                    "not_sensitive_ssl_param=value")
end
