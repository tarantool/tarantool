#!/usr/bin/env tarantool

--
-- gh-5632: Lua code should not use any global buffers or objects without
-- proper ownership protection. Otherwise these items might be suddenly reused
-- during Lua GC which happens almost at any moment. That might lead to data
-- corruption.
--

local tap = require('tap')
local ffi = require('ffi')
local uuid = require('uuid')
local uri = require('uri')

local function test_uuid(test)
    test:plan(1)

    local gc_count = 100
    local iter_count = 1000
    local is_success = true

    local function uuid_to_str()
        local uu = uuid.new()
        local str1 = uu:str()
        local str2 = uu:str()
        if str1 ~= str2 then
            is_success = false
            assert(false)
        end
    end

    local function create_gc()
        for _ = 1, gc_count do
            ffi.gc(ffi.new('char[1]'), function() uuid_to_str() end)
        end
    end

    for _ = 1, iter_count do
        create_gc()
        uuid_to_str()
    end

    test:ok(is_success, 'uuid in gc')
end

local function test_uri(test)
    test:plan(1)

    local gc_count = 100
    local iter_count = 1000
    local port = 1
    local ip = 1
    local login = 1
    local pass = 1
    local is_success = true

    local function uri_parse()
        local loc_ip = ip
        local loc_port = port
        local loc_pass = pass
        local loc_login = login

        ip = ip + 1
        port = port + 1
        pass = pass + 1
        login = login + 1
        if port > 60000 then
            port = 1
        end
        if ip > 255 then
            ip = 1
        end

        loc_ip = string.format('127.0.0.%s', loc_ip)
        loc_port = tostring(loc_port)
        loc_pass = string.format('password%s', loc_pass)
        loc_login = string.format('login%s', loc_login)
        local host = string.format('%s:%s@%s:%s', loc_login, loc_pass,
                                   loc_ip, loc_port)
        local u = uri.parse(host)
        if u.host ~= loc_ip or u.login ~= loc_login or u.service ~= loc_port or
           u.password ~= loc_pass then
            is_success = false
            assert(false)
        end
    end

    local function create_gc()
        for _ = 1, gc_count do
            ffi.gc(ffi.new('char[1]'), uri_parse)
        end
    end

    for _ = 1, iter_count do
        create_gc()
        uri_parse()
    end

    test:ok(is_success, 'uri in gc')
end

local test = tap.test('gh-5632-gc-buf-reuse')
test:plan(2)
test:test('uuid in __gc', test_uuid)
test:test('uri in __gc', test_uri)

os.exit(test:check() and 0 or 1)
