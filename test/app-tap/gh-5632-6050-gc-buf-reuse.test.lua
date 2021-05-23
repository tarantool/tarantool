#!/usr/bin/env tarantool

--
-- gh-5632, gh-6050: Lua code should not use any global buffers or objects
-- without proper ownership protection. Otherwise these items might be suddenly
-- reused during Lua GC which happens almost at any moment. That might lead to
-- data corruption.
--

local tap = require('tap')
local ffi = require('ffi')
local uuid = require('uuid')
local uri = require('uri')
local json = require('json')
local msgpackffi = require('msgpackffi')

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

local function test_msgpackffi(test)
    test:plan(1)

    local mp_encode = msgpackffi.encode
    local mp_decode = msgpackffi.decode
    local gc_count = 100
    local iter_count = 1000
    local is_success = true
    local data = {0, 1, 1000, 100000000, 'str', true, 1.1}

    local function do_encode()
        if not is_success then
            return
        end
        local t = mp_encode(data)
        t = mp_decode(t)
        if #t ~= #data then
            is_success = false
            return
        end
        for i = 1, #t do
            if t[i] ~= data[i] then
                is_success = false
                return
            end
        end
    end

    local function create_gc()
        for _ = 1, gc_count do
            ffi.gc(ffi.new('char[1]'), do_encode)
        end
    end

    for _ = 1, iter_count do
        create_gc()
        do_encode()
    end

    test:ok(is_success, 'msgpackffi in gc')
end

local function test_json(test)
    test:plan(1)

    local encode = json.encode
    local decode = json.decode
    local gc_count = 100
    local iter_count = 1000
    local is_success = true
    local data1 = {1, 2, 3, 4, 5}
    local data2 = {6, 7, 8, 9, 10}

    local function do_encode(data)
        if not is_success then
            return
        end
        local t = encode(data)
        t = decode(t)
        if #t ~= #data then
            is_success = false
            return
        end
        for i = 1, #t do
            if t[i] ~= data[i] then
                is_success = false
                return
            end
        end
    end

    local function gc_encode()
        return do_encode(data1)
    end

    local function create_gc()
        for _ = 1, gc_count do
            ffi.gc(ffi.new('char[1]'), gc_encode)
        end
    end

    for _ = 1, iter_count do
        create_gc()
        do_encode(data2)
    end

   test:ok(is_success, 'json in gc')
end

local test = tap.test('gh-5632-6050-gc-buf-reuse')
test:plan(4)
test:test('uuid in __gc', test_uuid)
test:test('uri in __gc', test_uri)
test:test('msgpackffi in __gc', test_msgpackffi)
test:test('json in __gc', test_json)

os.exit(test:check() and 0 or 1)
