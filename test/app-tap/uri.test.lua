#!/usr/bin/env tarantool

local tap = require('tap')
local uri = require('uri')
local ffi = require('ffi')
local static_alloc = require('buffer').static_alloc

local function test_parse(test)
    -- Tests for uri.parse() Lua bindings.
    -- Parser itself is tested by test/unit/uri unit test.
    test:plan(28)

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
    test:is(u.host, '127.0.0.1', 'ipv4')
    test:is(u.ipv4, '127.0.0.1', 'ipv4')

    u = uri.parse('[2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10]')
    test:is(u.host, '2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10', 'ipv6')
    test:is(u.ipv6, '2a00:1148:b0ba:2016:12bf:48ff:fe78:fd10', 'ipv6')

    u = uri.parse('/tmp/unix.sock')
    test:is(u.host, 'unix/', 'unix')
    test:is(u.service, '/tmp/unix.sock', 'unix')
    test:is(u.unix, '/tmp/unix.sock', 'unix')

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

local function test_static_alloc(test)
    -- gh-4779 uri.format returns junk output.
    -- As static allocator is also used in several places
    -- we should properly handle situation when output
    -- is zero-length string.
    -- Here we allocate the whole buffer,
    -- fill it with some "junk" bytes and
    -- check that result doesn't contain any of them.
    local buffer_size = 12288
    local buf = static_alloc('char', buffer_size)
    ffi.fill(buf, 512, string.byte('x'))

    local iterations = 10
    test:plan(iterations)
    for _ = 1, iterations do
        test:is(uri.format({}), '')
    end
end

tap.test("uri", function(test)
    test:plan(3)
    test:test("parse", test_parse)
    test:test("format", test_format)
    test:test("static_alloc", test_static_alloc)
end)
