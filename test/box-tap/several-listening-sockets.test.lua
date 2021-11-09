#!/usr/bin/env tarantool
local tap = require('tap')
local net_box = require('net.box')
local os = require('os')
local fio = require('fio')

-- Create table which contain several listening uri,
-- according to template @a addr. It's simple adds
-- capital letters in alphabetical order to the
-- template.
local function create_uri_table(addr, count)
    local uris_table = {}
    local path_table = {}
    local ascii_A = string.byte('A')
    for i = 1, count do
        local ascii_code = ascii_A + i - 1
        local letter = string.char(ascii_code)
        path_table[i] = addr .. letter
        uris_table[i] = "unix/:" .. addr .. letter
    end
    return uris_table, path_table
end

local function check_connection(port)
    local conn = net_box.connect(port)
    local rc = conn:ping()
    conn:close()
    return rc
end

local test = tap.test('gh-6535-listen-update-numeric-uri')
test:plan(44)

-- Check connection if listening uri passed as a single port number.
local port_number = 0
box.cfg{listen = port_number}
test:ok(check_connection(box.info.listen), "URI as a single port number")

-- Check connection if listening uri passed as a single string.
local unix_socket_path = "unix/:./tarantoolA"
box.cfg{listen = unix_socket_path}
test:ok(check_connection(unix_socket_path), "URI as a single string")
test:ok(box.cfg.listen == unix_socket_path, "box.cfg.listen")
test:ok(box.info.listen:match("unix/:"), "box.info.listen")

-- Check connection if listening uri passed as a table of port numbers.
local port_numbers = {0, 0, 0, 0, 0}
box.cfg{listen = port_numbers}
for i, _ in ipairs(port_numbers) do
    test:ok(check_connection(box.info.listen[i]), "URI as a table of numbers")
end

-- Check connection if listening uri passed as a table of strings.
local uri_table, path_table = create_uri_table("./tarantool", 5)
box.cfg{listen = uri_table}
for i, uri in ipairs(uri_table) do
    test:ok(check_connection(uri), "URI as a table of strings")
    test:ok(box.cfg.listen[i] == uri, "box.cfg.listen")
    test:ok(box.info.listen[i]:match("unix/:"), "box.info.listen")
    test:ok(fio.path.exists(path_table[i]), "fio.path.exists")
end

box.cfg{listen = ""}
for _, path in ipairs(path_table) do
    test:ok(not fio.path.exists(path), "fio.path.exists")
end

-- Special test case to check that all unix socket paths deleted
-- in case when `listen` fails because of invalid uri. Iproto performs
-- `bind` and `listen` operations sequentially to all uris from the list,
-- so we need to make sure that all resources for those uris for which
-- everything has already completed will be successfully cleared in case
-- of error for one of the next uri in list.
local uri_table, path_table = create_uri_table("./tarantool", 5)
table.insert(uri_table, "baduri:1")
table.insert(uri_table, "unix/:./tarantoolX")

-- can't resolve uri for bind
local ok, err = pcall(box.cfg, {listen = uri_table})
test:ok(not ok and err.message:match("can't resolve uri for bind"),
        "err.message:match")
for _, path in ipairs(path_table) do
    test:ok(not fio.path.exists(path), "fio.path.exists")
end
test:ok(not box.cfg.listen, "box.cfg.listen")

-- Special test case when we try to listen several identical URIs
local uri = "unix/:./tarantool"
local ok, err = pcall(box.cfg, {listen = {uri, uri, uri}})
test:ok(not ok and err.message:match('bind'), "err.message:match")
test:ok(not fio.path.exists(uri), "fio.path.exists")
test:ok(not box.cfg.listen, "box.cfg.listen")

os.exit(test:check() and 0 or 1)
