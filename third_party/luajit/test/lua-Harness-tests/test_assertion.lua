
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--

--[[

        Test Anything Protocol : minimalist version

]]

_dofile = _dofile or dofile     -- could be defined in profile

function _retrieve_progname ()
    local i = 0
    while arg[i] do
        i = i - 1
    end
    return arg[i + 1]
end

if pcall(require, 'Test.Assertion') then
    diag 'Test.Assertion loaded'
    return
end

local os = os
local pcall = pcall
local print = print
local require = require
local tostring = tostring
local type = type

local curr_test = 0
local expected_tests = 0
local todo_upto = 0
local todo_reason

function plan (arg)
    if arg ~= 'no_plan' then
        expected_tests = arg
        print("1.." .. tostring(arg))
    end
end

function done_testing ()
    print("1.." .. tostring(curr_test))
end

function skip_all (reason)
    out = "1..0"
    if reason then
        out = out .. " # SKIP " .. reason
    end
    print(out)
    os.exit(0)
end

function truthy (test, name)
    curr_test = curr_test + 1
    local out = ''
    if not test then
        out = "not "
    end
    out = out .. "ok " .. tostring(curr_test)
    if name then
        out = out .. " - " .. name
    end
    if todo_reason and todo_upto >= curr_test then
        out = out .. " # TODO # " .. todo_reason
    end
    print(out)
end

function falsy (test, name)
    truthy(not test, name)
end

function equals (got, expected, name)
    local pass = got == expected
    truthy(pass, name)
    if not pass then
        diag("         got: " .. tostring(got))
        diag("    expected: " .. tostring(expected))
    end
end

function not_equals (got, not_expected, name)
    local pass = got ~= not_expected
    truthy(pass, name)
    if not pass then
        diag("         got: " .. tostring(got))
        diag("    expected: anything else")
    end
end

function near (got, expected, tolerance, name)
    local pass = got >= (expected - tolerance) and got <= (expected + tolerance)
    truthy(pass, name)
    if not pass then
        diag("         got: " .. tostring(got))
        diag("    expected: " .. tostring(expected) .. " +/- " .. tostring(tolerance))
    end
end

function matches (got, pattern, name)
    local pass = tostring(got):match(pattern)
    truthy(pass, name)
    if not pass then
        diag("                  " .. tostring(got))
        diag("    doesn't match '" .. tostring(pattern) .. "'")
    end
end

local function is_type (t)
    return function (val, name)
        if type(val) == t then
            truthy(true, name)
        else
            truthy(false, name)
            diag("    " .. tostring(val) .. " isn't a '" .. t .."' it's a '" .. type(val) .. "'")
        end
    end
end
is_boolean = is_type('boolean')
is_cdata = is_type('cdata')
is_function = is_type('function')
is_number = is_type('number')
is_string = is_type('string')
is_table = is_type('table')
is_thread = is_type('thread')
is_userdata = is_type('userdata')

local function is_value (expected)
    return function (got, name)
        local pass = got == expected
        truthy(pass, name)
        if not pass then
            diag("         got: " .. tostring(got))
            diag("    expected: " .. tostring(expected))
        end
    end
end
is_nil = is_value(nil)
is_true = is_value(true)
is_false = is_value(false)

function passes (name)
    truthy(true, name)
end

function fails (name)
    truthy(false, name)
end

function require_ok (mod)
    local r, msg = pcall(require, mod)
    truthy(r, "require '" .. mod .. "'")
    if not r then
        diag("    " .. msg)
    end
    return r
end

function array_equals (got, expected, name)
    for i = 1, #expected do
        local v = expected[i]
        local val = got[i]
        if val ~= v then
            truthy(false, name)
            diag("    at index: " .. tostring(i))
            diag("         got: " .. tostring(val))
            diag("    expected: " .. tostring(v))
            return
        end
    end
    local extra = #got - #expected
    if extra ~= 0 then
        truthy(false, name)
        diag("    " .. tostring(extra) .. " unexpected item(s)")
    else
        truthy(true, name)
    end
end

function error_equals (code, expected, name)
    local r, msg = pcall(code)
    if r then
        truthy(false, name)
        diag("    unexpected success")
        diag("    expected: " .. tostring(pattern))
    else
        equals(msg, expected, name)
    end
end

function error_matches (code, pattern, name)
    local r, msg = pcall(code)
    if r then
        truthy(false, name)
        diag("    unexpected success")
        diag("    expected: " .. tostring(pattern))
    else
        matches(msg, pattern, name)
    end
end

function not_errors (code, name)
    local r, msg = pcall(code)
    truthy(r, name)
    if not r then
        diag("    " .. msg)
    end
end

function diag (msg)
    print("# " .. msg)
end

function skip (reason, count)
    count = count or 1
    local name = "# skip"
    if reason then
        name = name .. " " ..reason
    end
    for i = 1, count do
        truthy(true, name)
    end
end

function skip_rest (reason)
    skip(reason, expected_tests - curr_test)
end

function todo (reason, count)
    count = count or 1
    todo_upto = curr_test + count
    todo_reason = reason
end

--
-- Copyright (c) 2009-2021 Francois Perrad
--
-- This library is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--
