#!/usr/bin/env tarantool

local tap = require('tap')
local uri = require('uri')

local function test_parse(test)
    -- Tests for uri.parse() Lua bindings.
    -- Parser itself is tested by test/unit/uri_parser unit test.
    test:plan(56)

    local u

    u = uri.parse("scheme://login:password@host:service"..
        "/path1/path2/path3?q1=v1&q2=v2&q3=v3:1|v3:2#fragment")
    test:is(u.scheme, "scheme", "scheme")
    test:is(u.login, "login", "login")
    test:is(u.password, "password", "password")
    test:is(u.host, "host", "host")
    test:is(u.service, "service", "service")
    test:is(u.path, "/path1/path2/path3", "path")
    test:is(u.query, "q1=v1&q2=v2&q3=v3:1|v3:2", "query")
    test:is(u.fragment, "fragment", "fragment")

    u = uri.parse("scheme://login:@host:service"..
        "/path1/path2/path3?q1=v1&q2=v2&q3=v3:1|v3:2#fragment")
    test:is(u.scheme, "scheme", "scheme")
    test:is(u.login, "login", "login")
    test:is(u.password, "", "password")
    test:is(u.host, "host", "host")
    test:is(u.service, "service", "service")
    test:is(u.path, "/path1/path2/path3", "path")
    test:is(u.query, "q1=v1&q2=v2&q3=v3:1|v3:2", "query")
    test:is(u.fragment, "fragment", "fragment")

    u = uri.parse('login@host')
    test:is(u.login, "login", "login")
    test:is(u.password, nil, "password")
    test:is(u.host, "host", "host")

    u = uri.parse('127.0.0.1')
    test:is(u.host, '127.0.0.1', 'host')
    test:is(u.ipv4, '127.0.0.1', 'ipv4')

    u = uri.parse('[2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10]')
    test:is(u.host, '2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10', 'host')
    test:is(u.ipv6, '2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10', 'ipv6')

    u = uri.parse('/tmp/unix.sock')
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')

    u = uri.parse("/tmp/unix.sock?q1=v1&q2=v2#fragment")
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(u.query, 'q1=v1&q2=v2', 'query')
    test:is(u.fragment, 'fragment', 'fragment')

    u = uri.parse("/tmp/unix.sock:/path1/path2/path3?q1=v1&q2=v2#fragment")
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(u.path, '/path1/path2/path3', 'path')
    test:is(u.query, 'q1=v1&q2=v2', 'query')
    test:is(u.fragment, 'fragment', 'fragment')

    u = uri.parse("login:password@/tmp/unix.sock:" ..
                  "/path1/path2/path3?q1=v1#fragment")
    test:is(u.login, 'login', 'login')
    test:is(u.password, 'password', 'password')
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(u.path, '/path1/path2/path3', 'path')
    test:is(u.query, 'q1=v1', 'query')
    test:is(u.fragment, 'fragment', 'fragment')

    u = uri.parse("scheme://login:password@/tmp/unix.sock:/path1/path2/path3")
    test:is(u.scheme, 'scheme', 'scheme')
    test:is(u.login, 'login', 'login')
    test:is(u.password, 'password', 'password')
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(u.path, '/path1/path2/path3', 'path')

    local error, expected_errmsg

    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    u, error = uri.parse("")
    test:isnil(u, "invalid uri", u)
    test:is(tostring(error), expected_errmsg, "error message")
    u, error = uri.parse("://")
    test:isnil(u, "invalid uri", u)
    test:is(tostring(error), expected_errmsg, "error message")
end

local function test_format(test)
    test:plan(13)
    local u = uri.parse("user:password@localhost")
    test:is(uri.format(u), "user@localhost", "password removed")
    test:is(uri.format(u, false), "user@localhost", "password removed")
    test:is(uri.format(u, true), "user:password@localhost", "password kept")

    -- URI with empty query
     u = uri.parse({"/tmp/unix.sock?"})
     test:is(uri.format(u), "unix/:/tmp/unix.sock", "URI format")

     -- URI with one empty parameter
    u = uri.parse({"/tmp/unix.sock?q1"})
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1", "URI format")

    -- All parameters passed in "params" table.
    u = uri.parse({"/tmp/unix.sock", params = {q1 = "v1", q2 = "v2"}})
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1=v1&q2=v2", "URI format")

    -- Empty parameter in URI string.
    u = uri.parse({"/tmp/unix.sock?q1", params = {q2 = "v2", q3 = "v3"}})
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1&q2=v2&q3=v3", "URI format")

    -- Parameter without value in URI string.
    u = uri.parse({"/tmp/unix.sock?q1=", params = {q2 = "v2", q3 = "v3"}})
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1=&q2=v2&q3=v3", "URI format")

    -- Some parameters passed in URI string and some different
    -- parameters passed in "params" table.
    u = uri.parse({"/tmp/unix.sock?q1=v1", params = {q2 = "v2"}})
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1=v1&q2=v2", "URI format")

    -- Same as previous but each parameter has several values.
    u = uri.parse({
        "/tmp/unix.sock?q1=v11&q1=v12",
        params = {q2 = {"v21", "v22"}}
    })
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1=v11&q1=v12&q2=v21&q2=v22",
            "URI format")

    -- One of parameters in "param" table has empty value
    u = uri.parse({
        "/tmp/unix.sock?q1=v11&q1=v12",
        params = {q2 = {""}}
    })
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1=v11&q1=v12&q2=", "URI format")

    -- Parameter from "params" table overwrite
    -- parameter from URI string.
    u = uri.parse({
        "/tmp/unix.sock?q1=v11&q1=v12",
        params = {q1 = {"v13", "v14"}, q2 = "v2"}
    })
    test:is(uri.format(u), "unix/:/tmp/unix.sock?q1=v13&q1=v14&q2=v2", "URI format")

    test:is(uri.format{host = "unix/", service = "/tmp/unix.sock"},
            "unix/:/tmp/unix.sock", "URI format")
end

local function test_parse_uri_query_params(test)
    -- Tests for uri.parse() Lua bindings (URI with query parameters).
    -- Parser itself is tested by test/unit/uri unit test.
    test:plan(55)

    local u

    u = uri.parse("/tmp/unix.sock?q1=v1")
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(type(u.params["q1"]), "table", "name")
    test:is(#u.params["q1"], 1, "value count")
    test:is(u.params["q1"][1], "v1", "param value")

    u = uri.parse("/tmp/unix.sock?q1=v1&q1=v2")
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(type(u.params["q1"]), "table", "name")
    test:is(#u.params["q1"], 2, "value count")
    test:is(u.params["q1"][1], "v1", "param value")
    test:is(u.params["q1"][2], "v2", "param value")

    u = uri.parse("/tmp/unix.sock?q1=v11&q1=v12&q2=v21&q2=v22")
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(type(u.params["q1"]), "table", "name")
    test:is(#u.params["q1"], 2, "value count")
    test:is(u.params["q1"][1], "v11", "param value")
    test:is(u.params["q1"][2], "v12", "param value")
    test:is(type(u.params["q2"]), "table", "name")
    test:is(#u.params["q2"], 2, "value count")
    test:is(u.params["q2"][1], "v21", "param value")
    test:is(u.params["q2"][2], "v22", "param value")

    u = uri.parse("/tmp/unix.sock?q1=v1&q1=&q2")
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(type(u.params["q1"]), "table", "name")
    test:is(#u.params["q1"], 2, "value count")
    test:is(u.params["q1"][1], "v1", "param value")
    test:is(u.params["q1"][2], "", "param value")
    test:is(type(u.params["q2"]), "table", "name")
    test:is(#u.params["q2"], 0, "value count")

    -- Parse URI passed in table format, parameter values
    -- from "params" table, overwrite values from string.
    u = uri.parse({
        "/tmp/unix.sock?q1=v11",
        params = { q1 = "v12", q2 = "v21" }
    })
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(type(u.params["q1"]), "table", "name")
    test:is(#u.params["q1"], 1, "value count")
    test:is(u.params["q1"][1], "v12", "param value")
    test:is(type(u.params["q2"]), "table", "name")
    test:is(#u.params["q2"], 1, "value count")
    test:is(u.params["q2"][1], "v21", "param value")

    -- Same as previous but "uri=" syntax
    u = uri.parse({
        uri = "/tmp/unix.sock?q1=v11",
        params = { q1 = "v12", q2 = "v21" }
    })
    test:is(u.host, 'unix/', 'host')
    test:is(u.service, '/tmp/unix.sock', 'service')
    test:is(u.unix, '/tmp/unix.sock', 'unix')
    test:is(type(u.params["q1"]), "table", "name")
    test:is(#u.params["q1"], 1, "value count")
    test:is(u.params["q1"][1], "v12", "param value")
    test:is(type(u.params["q2"]), "table", "name")
    test:is(#u.params["q2"], 1, "value count")
    test:is(u.params["q2"][1], "v21", "param value")

    local error, expected_errmsg

    -- "defult_params" is not allowed for single URI.
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    u, error = uri.parse({ "/tmp/unix.sock", default_params = {q = "v"} })
    test:isnil(u, "invalid uri", u)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Multiple URIs is not allowed in `parse` method,
    -- use `parse_many` instead.
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    u, error = uri.parse({ "/tmp/unix.sock, /tmp/unix.sock"})
    test:isnil(u, "invalid uri", u)
    test:is(tostring(error), expected_errmsg, "error message")
end

local function test_parse_uri_set_with_query_params(test)
    -- Tests for uri.parse_many() Lua bindings (several URIs with query parameters).
    -- Parser itself is tested by test/unit/uri unit test.
    test:plan(57)

    local uri_set

    -- The new method can work fine with both one and several URIs.
    uri_set = uri.parse_many("/tmp/unix.sock?q=v")
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q"]), "table", "name")
    test:is(#uri_set[1].params["q"], 1, "value count")
    test:is(uri_set[1].params["q"][1], "v", "param value")

    uri_set = uri.parse_many("/tmp/unix.sock?q=1, /tmp/unix.sock?q=2")
    test:is(#uri_set, 2, "uri count")
    for i = 1, #uri_set do
        test:is(uri_set[i].host, 'unix/', 'host')
        test:is(uri_set[i].service, '/tmp/unix.sock', 'service')
        test:is(uri_set[i].unix, '/tmp/unix.sock', 'unix')
        test:is(type(uri_set[i].params["q"]), "table", "name")
        test:is(#uri_set[i].params["q"], 1, "value count")
        test:is(uri_set[i].params["q"][1], tostring(i), "param value")
    end

    uri_set = uri.parse_many("/tmp/unix.sock?q1=1&q1=&q2&q3=" .. "," ..
                             "/tmp/unix.sock?q1=1&q1=&q2&q3=")
    test:is(#uri_set, 2, "uri count")
    for i = 1, #uri_set do
        test:is(uri_set[i].host, 'unix/', 'host')
        test:is(uri_set[i].service, '/tmp/unix.sock', 'service')
        test:is(uri_set[i].unix, '/tmp/unix.sock', 'unix')
        test:is(type(uri_set[i].params["q1"]), "table", "name")
        test:is(#uri_set[i].params["q1"], 2, "value count")
        test:is(uri_set[i].params["q1"][1], "1", "param value")
        test:is(uri_set[i].params["q1"][2], "", "param value")
        test:is(type(uri_set[i].params["q2"]), "table", "name")
        test:is(#uri_set[i].params["q2"], 0, "value count")
        test:is(type(uri_set[i].params["q3"]), "table", "name")
        test:is(#uri_set[i].params["q3"], 1, "value count")
        test:is(uri_set[i].params["q3"][1], "", "value")
    end

    -- Empty string means empty uri_set
    uri_set = uri.parse_many("")
    test:is(#uri_set, 0, "uri_set")

    local error, expected_errmsg

    -- Check that uri.parse_many return nil for invalid URIs string
    uri_set = uri.parse_many("tmp/unix.sock, ://")
    test:isnil(uri_set, "invalid uri", uri_set)

    -- Extra ',' is not allowed
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    uri_set , error= uri.parse_many("/tmp/unix.sock,,/tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    uri_set, error = uri.parse_many("/tmp/unix.sock, ,/tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    uri_set, error = uri.parse_many("/tmp/unix.sock,, /tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    uri_set, error = uri.parse_many("/tmp/unix.sock ,,/tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")


    -- Check that we can't parse string with multiple URIs,
    -- using old method.
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    local u, error = uri.parse("/tmp/unix.sock, /tmp/unix.sock")
    test:isnil(u, "invalid uri", u)
    test:is(tostring(error), expected_errmsg, "error message")
end

local function test_parse_uri_set_from_lua_table(test)
    -- Tests for uri.parse_many() Lua bindings.
    -- (Several URIs with parameters, passed in different ways).
    test:plan(132)

    local uri_set

    -- Array with one string address and one parameter
    uri_set = uri.parse_many({"/tmp/unix.sock?q1=v1"})
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q1"]), "table", "name")
    test:is(#uri_set[1].params["q1"], 1, "value count")
    test:is(uri_set[1].params["q1"][1], "v1", "param value")

    -- Array with one string address and several parameters.
    -- One of them passed in URI string, one separately as a string,
    -- one separately as a table with two values.
    uri_set = uri.parse_many({
        "/tmp/unix.sock?q1=v1",
        params = {q2 = "v2", q3 = {"v31", "v32"}}
    })
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q1"]), "table", "name")
    test:is(#uri_set[1].params["q1"], 1, "value count")
    test:is(uri_set[1].params["q1"][1], "v1", "param value")
    test:is(type(uri_set[1].params["q2"]), "table", "name")
    test:is(#uri_set[1].params["q2"], 1, "value count")
    test:is(uri_set[1].params["q2"][1], "v2", "param value")
    test:is(type(uri_set[1].params["q3"]), "table", "name")
    test:is(#uri_set[1].params["q3"], 2, "value count")
    test:is(uri_set[1].params["q3"][1], "v31", "param value")
    test:is(uri_set[1].params["q3"][2], "v32", "param value")

    -- Same as previous but use "uri=" syntax to save URI value.
    uri_set = uri.parse_many({
        uri = "/tmp/unix.sock?q1=v1",
        params = {q2 = "v2", q3 = {"v31", "v32"}}
    })
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q1"]), "table", "name")
    test:is(#uri_set[1].params["q1"], 1, "value count")
    test:is(uri_set[1].params["q1"][1], "v1", "param value")
    test:is(type(uri_set[1].params["q2"]), "table", "name")
    test:is(#uri_set[1].params["q2"], 1, "value count")
    test:is(uri_set[1].params["q2"][1], "v2", "param value")
    test:is(type(uri_set[1].params["q3"]), "table", "name")
    test:is(#uri_set[1].params["q3"], 2, "value count")
    test:is(uri_set[1].params["q3"][1], "v31", "param value")
    test:is(uri_set[1].params["q3"][2], "v32", "param value")

    -- Check that URI parameter value from "params" table
    -- overwrite parameter value from the string.
    uri_set = uri.parse_many({
        "/tmp/unix.sock?q1=v1",
        params = {q1 = {"v2", "v3"}}
    })
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q1"]), "table", "name")
    test:is(#uri_set[1].params["q1"], 2, "value count")
    -- "v1" value was overwriten by values from "params" table
    test:is(uri_set[1].params["q1"][1], "v2", "param value")
    test:is(uri_set[1].params["q1"][2], "v3", "param value")

    -- Most common way: several URIs passed as array of strings
    -- and objects with different parameters and default parameters.
    uri_set = uri.parse_many({
        "/tmp/unix.sock?q1=v11",
        {  "/tmp/unix.sock?q2=v21", params = { q2 = "v22", q3 = "v31" } },
        { "/tmp/unix.sock", params = { q1 = 1, q2 = { 2, 3, 4}, q3 = 5 } },
        { uri = "/tmp/unix.sock?q2&q3=v31", params = { q3 = {"v33", "v34"} } },
        default_params = {
            q1 = {"v12", "v13"}, q2 = {"v21", "v22"}, q3 = {"v32"}, q4 = 6
        }
    })
    test:is(#uri_set, 4, "uri count")
    -- First URI from uri_set
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q1"]), "table", "name")
    test:is(#uri_set[1].params["q1"], 1, "value count")
    -- As previously values from "default_params" table are
    -- ignored if there is some values for this URI parameter
    -- from URI string or "params" table.
    test:is(uri_set[1].params["q1"][1], "v11", "param value")
    test:is(type(uri_set[1].params["q2"]), "table", "name")
    test:is(#uri_set[1].params["q2"], 2, "value count")
    -- Values was added from "default_params" table.
    test:is(uri_set[1].params["q2"][1], "v21", "param value")
    test:is(uri_set[1].params["q2"][2], "v22", "param value")
    test:is(type(uri_set[1].params["q3"]), "table", "name")
    test:is(#uri_set[1].params["q3"], 1, "value count")
    -- Value was added from "params" table.
    test:is(uri_set[1].params["q3"][1], "v32", "param value")
    test:is(type(uri_set[1].params["q4"]), "table", "name")
    test:is(#uri_set[1].params["q4"], 1, "value count")
    -- Numerical value, saved as a string.
    test:is(uri_set[1].params["q4"][1], "6", "param value")
    -- Second URI from uri_set
    test:is(uri_set[2].host, 'unix/', 'host')
    test:is(uri_set[2].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[2].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[2].params["q1"]), "table", "name")
    test:is(#uri_set[2].params["q1"], 2, "value count")
    -- Values from "default_params" table for "q1" parameter
    -- are added, because there is no such parameter in URI
    -- string and in "params" table.
    test:is(uri_set[2].params["q1"][1], "v12", "param value")
    test:is(uri_set[2].params["q1"][1], "v12", "param value")
    test:is(type(uri_set[2].params["q2"]), "table", "name")
    test:is(#uri_set[2].params["q2"], 1, "value count")
    -- "q2" parameter value from URI string overwritten by
    -- value from "params" table, values from "defaul_params"
    -- table are ignored.
    test:is(uri_set[2].params["q2"][1], "v22", "param value")
    test:is(type(uri_set[2].params["q3"]), "table", "name")
    test:is(#uri_set[2].params["q3"], 1, "value count")
    -- "q3" parameter value added from from "params" table,
    -- values from "defaul_params" table are ignored.
    test:is(uri_set[2].params["q3"][1], "v31", "param value")
    test:is(type(uri_set[2].params["q4"]), "table", "name")
    test:is(#uri_set[2].params["q4"], 1, "value count")
    -- Numerical value, saved as a string.
    test:is(uri_set[2].params["q4"][1], "6", "param value")
    -- Third URI from uri_set
    -- All values are taken from "params" table, just check
    -- how we parse numerical parameter values.
    test:is(uri_set[3].host, 'unix/', 'host')
    test:is(uri_set[3].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[3].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[3].params["q1"]), "table", "name")
    test:is(#uri_set[3].params["q1"], 1, "value count")
    test:is(uri_set[3].params["q1"][1], "1", "param value")
    test:is(type(uri_set[3].params["q2"]), "table", "name")
    test:is(#uri_set[3].params["q2"], 3, "value count")
    test:is(uri_set[3].params["q2"][1], "2", "param value")
    test:is(uri_set[3].params["q2"][2], "3", "param value")
    test:is(uri_set[3].params["q2"][3], "4", "param value")
    test:is(type(uri_set[3].params["q3"]), "table", "name")
    test:is(#uri_set[3].params["q3"], 1, "value count")
    test:is(uri_set[3].params["q3"][1], "5", "param value")
    -- Fourth URI from uri_set
    test:is(uri_set[4].host, 'unix/', 'host')
    test:is(uri_set[4].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[4].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[4].params["q1"]), "table", "name")
    test:is(#uri_set[4].params["q1"], 2, "value count")
    -- As previous values was added from "default_params" table
    test:is(uri_set[4].params["q1"][1], "v12", "param value")
    test:is(uri_set[4].params["q1"][2], "v13", "param value")
    test:is(type(uri_set[4].params["q2"]), "table", "name")
    test:is(#uri_set[4].params["q2"], 2, "value count")
    -- Values from "default_params" table for "q2" parameter
    -- are added, because there is no values for this parameter
    -- in URI string and there is no such paramater in "params
    -- table.
    test:is(uri_set[4].params["q2"][1], "v21", "param value")
    test:is(uri_set[4].params["q2"][2], "v22", "param value")
    test:is(type(uri_set[4].params["q3"]), "table", "name")
    test:is(#uri_set[4].params["q3"], 2, "value count")
    -- Value from URI string was overwritten by values from
    -- "params" table. Values from "default_params" table are
    -- ignored.
    test:is(uri_set[4].params["q3"][1], "v33", "param value")
    test:is(uri_set[4].params["q3"][2], "v34", "param value")
    test:is(type(uri_set[4].params["q4"]), "table", "name")
    test:is(#uri_set[4].params["q4"], 1, "value count")
    -- Numerical value, saved as a string.
    test:is(uri_set[4].params["q4"][1], "6", "param value")

    -- If some URI parameter is a table in "params"
    -- or "default_params" table, all keys in this
    -- table should be numerical, otherwise silently
    -- ignored
    uri_set = uri.parse_many({
        { "/tmp/unix.sock", params = {q1 = { x = "y"}} },
        "/tmp/unix.sock",
        default_params = {q2 = {x = "y"}}
    })
    test:is(#uri_set, 2, "uri count")
    -- First URI from uri_set
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params), "nil", "params")
    test:is(uri_set[2].host, 'unix/', 'host')
    test:is(uri_set[2].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[2].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params), "nil", "params")

    -- URI table without URI
    uri_set = uri.parse_many({
        params = {q = "v"},
        default_params = {}
    })
    test:is(#uri_set, 0, "uri count")

    -- URI table with one URI in table format
    uri_set = uri.parse_many{{"/tmp/unix.sock"}, default_params = {q = "v"}}
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q"]), "table", "name")
    test:is(#uri_set[1].params["q"], 1, "value count")
    test:is(uri_set[1].params["q"][1], "v", "param value")

    -- Same as previous but with "uri=" syntax
    uri_set = uri.parse_many{{uri = "/tmp/unix.sock"}, default_params = {q = "v"}}
    test:is(#uri_set, 1, "uri count")
    test:is(uri_set[1].host, 'unix/', 'host')
    test:is(uri_set[1].service, '/tmp/unix.sock', 'service')
    test:is(uri_set[1].unix, '/tmp/unix.sock', 'unix')
    test:is(type(uri_set[1].params["q"]), "table", "name")
    test:is(#uri_set[1].params["q"], 1, "value count")
    test:is(uri_set[1].params["q"][1], "v", "param value")
end

local function test_parse_invalid_uri_set_from_lua_table(test)
    -- Tests for uri.parse_many() Lua bindings.
    -- (Several invalid URIs with parameters, passed in different ways).
    test:plan(50)

    local uri_set, error, expected_errmsg

    -- Invalid type passed to "parse_many"
    expected_errmsg = "Incorrect type for URI: should be string, number or table"
    uri_set, error = uri.parse_many(function() end)
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Invalid type of value for numerical key
    expected_errmsg = "Incorrect type for URI: should be string, number or table"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", function() end})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Invalid type of value for string keys
    expected_errmsg = "Invalid URI table: expected " ..
                      "{uri = string, params = table}" .. " or " ..
                      "{string, params = table}"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", uri = function() end})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Incorrect type for URI parameters: should be a table"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", params = function() end})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Incorrect type for URI parameters: should be a table"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", params = ""})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", default_params = ""})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", default_params = ""})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")

    -- Mix "uri=" and numerical keys is banned
    expected_errmsg = "Invalid URI table: expected " ..
                      "{uri = string, params = table}" .. " or " ..
                      "{string, params = table}"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", uri = "/tmp/unix.sock"})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Several URIs in one string is allowed only when the
    -- passed as a single string.
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    uri_set, error = uri.parse_many({"/tmp/unix.sock, /tmp/unix.sock"})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- "params" table is allowed only for single URI
    expected_errmsg = "URI parameters are not allowed for multiple URIs"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock", "/tmp/unix.sock",
        params = {q1 = "v1"}
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- "params" table is not allowed with nested tables
    expected_errmsg = "URI parameters are not allowed for multiple URIs"
    uri_set, error = uri.parse_many({
        {"/tmp/unix.sock"},
        params = {q1 = "v1"}
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- "default_params" table is not allowed in nested URI tables
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({{"/tmp/unix.sock", default_params = {}}})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- "default_params" table is not allowed for single URI
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({"/tmp/unix.sock", default_params = {}})
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Only one URI is allowed in nested tables
    expected_errmsg = "Invalid URI table: expected " ..
                      "{uri = string, params = table}" .. " or " ..
                      "{string, params = table}"
    uri_set, error = uri.parse_many({
        {"/tmp/unix.sock", "/tmp/unix.sock"},
        default_params = {q = "v"}
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Nested URI tables is not allowed in nested tables
    expected_errmsg = "Invalid URI table: expected "..
                      "{uri = string, params = table}" .. " or " ..
                      "{string, params = table}"
    uri_set, error = uri.parse_many({
        {"/tmp/unix.sock", {}}
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Nested URI table without URI is now allowed
    expected_errmsg = "Invalid URI table: expected "..
                      "{uri = string, params = table}" .. " or " ..
                      "{string, params = table}"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        { params = {q = "v"} }
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Only string key types are allowed in "params" and
    -- "default_params" table
    expected_errmsg = "Incorrect type for URI parameter name: " ..
                      "should be a string"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        params = {"v"},
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        default_params = {"v"},
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Invalid type of values in "params" and
    -- "default_params" table
    expected_errmsg = "Incorrect type for URI parameter value: " ..
                      "should be string, number or table"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        params = {q = function() end},
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        default_params = {q = function() end},
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Incorrect type for URI parameter value: "..
                      "should be string or number"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        params = {q = {function() end}},
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    expected_errmsg = "Default URI parameters are not allowed for single URI"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        default_params = {q = {function() end}},
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Invalid uri string in URIs table
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        "://"
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Invalid uri in nested URI table
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        {"://"}
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
    -- Same as previous but with "uri=" syntax
    expected_errmsg = "Incorrect URI: expected host:service or /unix.socket"
    uri_set, error = uri.parse_many({
        "/tmp/unix.sock",
        {uri = "://"}
    })
    test:isnil(uri_set, "invalid uri", uri_set)
    test:is(tostring(error), expected_errmsg, "error message")
end

tap.test("uri", function(test)
    test:plan(6)
    test:test("parse", test_parse)
    test:test("parse URI query params", test_parse_uri_query_params)
    test:test("parse URIs with query params", test_parse_uri_set_with_query_params)
    test:test("parse URIs from lua table", test_parse_uri_set_from_lua_table)
    test:test("parse invalid URIs from lua table", test_parse_invalid_uri_set_from_lua_table)
    test:test("format", test_format)
end)
