#!/usr/bin/env tarantool

local tap = require('tap')
local uri = require('uri')

local function test_parse(test)
    -- Tests for uri.parse() Lua bindings.
    -- Parser itself is tested by test/unit/uri_parser unit test.
    test:plan(54)

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

    u = uri.parse("")
    test:isnil(u, "invalid uri", u)
    u = uri.parse("://")
    test:isnil(u, "invalid uri", u)
end

local function test_format(test)
    test:plan(3)
    local u = uri.parse("user:password@localhost")
    test:is(uri.format(u), "user@localhost", "password removed")
    test:is(uri.format(u, false), "user@localhost", "password removed")
    test:is(uri.format(u, true), "user:password@localhost", "password kept")
end

local function test_parse_uri_query_params(test)
    -- Tests for uri.parse() Lua bindings (URI with query parameters).
    -- Parser itself is tested by test/unit/uri unit test.
    test:plan(33)

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
end

local function test_parse_uri_set_with_query_params(test)
    -- Tests for uri.parse_many() Lua bindings (several URIs with query parameters).
    -- Parser itself is tested by test/unit/uri unit test.
    test:plan(52)

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

    -- Check that uri.parse_many return nil for invalid URIs string
    uri_set = uri.parse_many("tmp/unix.sock, ://")
    test:isnil(uri_set, "invalid uri", uri_set)

    -- Extra ',' is not allowed
    uri_set = uri.parse_many("/tmp/unix.sock,,/tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    uri_set = uri.parse_many("/tmp/unix.sock, ,/tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    uri_set = uri.parse_many("/tmp/unix.sock,, /tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)
    uri_set = uri.parse_many("/tmp/unix.sock ,,/tmp/unix.sock")
    test:isnil(uri_set, "invalid uri", uri_set)


    -- Check that we can't parse string with multiple URIs,
    -- using old method.
    local u = uri.parse("/tmp/unix.sock, /tmp/unix.sock")
    test:isnil(u, "invalid uri", u)
end

tap.test("uri", function(test)
    test:plan(4)
    test:test("parse", test_parse)
    test:test("parse URI query params", test_parse_uri_query_params)
    test:test("parse URIs with query params", test_parse_uri_set_with_query_params)
    test:test("format", test_format)
end)
