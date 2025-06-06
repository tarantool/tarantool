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

local function check_connection(uris)
    if type(uris) == "string" then
        uris = {uris}
    end
    for _, uri in ipairs(uris) do
        local conn = net_box.connect(uri)
        local rc = conn:ping()
        conn:close()
        if not rc then
            return false
        end
    end
    return true
end

local test = tap.test('gh-6535-listen-update-numeric-uri')
test:plan(115)

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
test:istable(box.info.listen, "box.info.listen is a table")
test:ok(#box.info.listen / #port_numbers >= 1, "All ports are bound")
test:ok(#box.info.listen % #port_numbers == 0, "All protocols are bound")
test:ok(check_connection(box.info.listen), "URI as a table of numbers")

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

-- Test new ability to pass several URIs in different ways.
local uri = require('uri')
local uris = {
    "unix/:./tarantoolA",
    { "unix/:./tarantoolB"},
    { uri = "unix/:./tarantoolC"},
}
local uri_table = uri.parse_many(uris)
local path_table = {}
for i = 1, #uri_table do
    path_table[i] = uri_table[i].service
end
box.cfg{listen = uris}
for i, uri in ipairs(uri_table) do
    test:ok(check_connection("unix/:" .. uri.service), "URIs in multiple ways")
    test:ok(box.cfg.listen[i] == uris[i], "box.cfg.listen")
    test:ok(box.info.listen[i]:match("unix/:"), "box.info.listen")
    test:ok(fio.path.exists(path_table[i]), "fio.path.exists")
end

-- Special test case: empty  URI table means stop listening
box.cfg{listen = {}}
for _, path in ipairs(path_table) do
    test:ok(not fio.path.exists(path), "fio.path.exists")
end
test:is(box.info.listen, nil, "box.info.listen")
box.cfg{listen = ""}

-- Test error messages for a new way of passing multiple URIs
local unix_sock_path = "unix/:./tarantool"
local uri_table, errmsg
local function test_error_message(uri_table, errmsg)
    errmsg = "Incorrect value for option 'listen': " .. errmsg
    local _, err = pcall(box.cfg, {listen = uri_table})
    test:is(tostring(err), errmsg, "error message")
    _, err = pcall(box.cfg, {listen = uri_table})
    test:is(tostring(err), errmsg, "error message")
end

-- Incorrect URI: expected host:service or /unix.socket
uri_table = "://"
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

-- Several URIs, separated by commas passed as a single string
-- Failed to parse one of them.
uri_table = unix_socket_path .. ", ://, " .. unix_sock_path
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

-- Tests for cases when URI is passed in a table,
-- with size equal to one
-- Several URIs is banned in the same table.
uri_table = {uri = unix_socket_path, unix_sock_path}
errmsg = "Invalid URI table: expected {uri = string, params = table} or " ..
         "{string, params = table}"
test_error_message(uri_table, errmsg)

-- Default params is banned for single URI
uri_table = {unix_sock_path, default_params = {q = "v"}}
errmsg = "Default URI parameters are not allowed for single URI"
test_error_message(uri_table, errmsg)

-- Incorrect URI: expected host:service or /unix.socket
uri_table = {"://"}
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

uri_table = {uri = "://"}
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

-- Function is not allowed type
uri_table = {function() end}
errmsg = "Incorrect type for URI in nested table: should be string, number"
test_error_message(uri_table, errmsg)

uri_table = {uri = function() end}
errmsg = "Incorrect type for URI in nested table: should be string, number"
test_error_message(uri_table, errmsg)

-- URI parameters should be passed in lua table
uri_table = {unix_sock_path, params = function()end}
errmsg = "Incorrect type for URI parameters: should be a table"
test_error_message(uri_table, errmsg)

-- URI parameter name should be a string
uri_table = {unix_sock_path, params = {"x"}}
errmsg = "Incorrect type for URI parameter name: should be a string"
test_error_message(uri_table, errmsg)

-- URI parameter should be one of types: string, number, table
uri_table = {unix_sock_path, params = {q = function()end}}
errmsg = "Incorrect type for URI parameter value: " ..
         "should be string, number or table"
test_error_message(uri_table, errmsg)

-- "URI parameter value should be one of types: string, number
uri_table = {unix_sock_path, params = {q = {function()end}}}
errmsg = "Incorrect type for URI parameter value: " ..
         "should be string or number"
test_error_message(uri_table, errmsg)

-- Tests for cases when URIs is passed in a most common way:
-- as a table, which contains string URIs and URIs in table format
-- Incorrect type for URI: should be string, number or table
uri_table = {unix_sock_path, {unix_sock_path}, function() end}
errmsg = "Incorrect type for URI: should be string, number or table"
test_error_message(uri_table, errmsg)

-- Failed to parse string URI
uri_table = {unix_sock_path, "://"}
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

-- Tests for nested tables
-- Several URIs in nested table is not allowed
uri_table = {unix_sock_path, {unix_sock_path, unix_sock_path}}
errmsg = "Invalid URI table: expected {uri = string, params = table} " ..
         "or {string, params = table}"
test_error_message(uri_table, errmsg)

-- Same as previous but with "uri=" syntax
uri_table = {unix_sock_path, {unix_sock_path, uri = unix_sock_path}}
errmsg = "Invalid URI table: expected {uri = string, params = table} " ..
         "or {string, params = table}"
test_error_message(uri_table, errmsg)

-- Missing URI is not allowed in ensted table
uri_table = {unix_sock_path, {}}
errmsg = "Invalid URI table: expected {uri = string, params = table} " ..
         "or {string, params = table}"
test_error_message(uri_table, errmsg)

-- Default params is not allowed in nested table
uri_table = {unix_sock_path, {unix_sock_path, default_params = {q = "v"}}}
errmsg = "Default URI parameters are not allowed for single URI"
test_error_message(uri_table, errmsg)

-- Invalid URI in nested table
uri_table = {unix_sock_path, {"://"}}
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

-- Same as previous, but with "uri=" syntax
uri_table = {unix_sock_path, {uri = "://"}}
errmsg = "Incorrect URI: expected host:service or /unix.socket"
test_error_message(uri_table, errmsg)

-- URI in nested table should be one of types: string, number
uri_table = {unix_sock_path, {function()end}}
errmsg = "Incorrect type for URI in nested table: should be string, number"
test_error_message(uri_table, errmsg)

-- Same as previous, but with "uri=" syntax
uri_table = {unix_sock_path, {uri = function()end}}
errmsg = "Incorrect type for URI in nested table: should be string, number"
test_error_message(uri_table, errmsg)

-- URI parameters should be passed in lua table
uri_table = {unix_sock_path, {unix_sock_path, params = function()end}}
errmsg = "Incorrect type for URI parameters: should be a table"
test_error_message(uri_table, errmsg)

-- URI parameter name should be a string
uri_table = {unix_sock_path, {unix_sock_path, params = {"q"}}}
errmsg = "Incorrect type for URI parameter name: should be a string"
test_error_message(uri_table, errmsg)

-- URI parameter should be one of types: string, number, table
uri_table = {unix_sock_path, {unix_sock_path, params = {q = function()end}}}
errmsg = "Incorrect type for URI parameter value: " ..
         "should be string, number or table"
test_error_message(uri_table, errmsg)

-- URI parameter value should be one of types: string, number
uri_table = {unix_sock_path, {unix_sock_path, params = {q = {{"v"}}}}}
errmsg = "Incorrect type for URI parameter value: " ..
         "should be string or number"
test_error_message(uri_table, errmsg)

-- Invalid URI table
uri_table = {unix_sock_path, unix_sock_path, uri = unix_sock_path}
errmsg = "Invalid URI table: expected {uri = string, params = table} " ..
         "or {string, params = table}"
test_error_message(uri_table, errmsg)

-- URI parameters arenot allowed for multiple URIs
uri_table = {unix_sock_path, unix_sock_path, params = {q = "v"}}
errmsg = "URI parameters are not allowed for multiple URIs"
test_error_message(uri_table, errmsg)

-- Test default URI parameters

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
