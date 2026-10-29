--- tap.lua internal file
---
--- The Test Anything Protocol version 13 producer
---

---# Builtin `tap` module
---
---The Test Anything Protocol version 13 producer.
---
---The `tap` module streamlines the testing of other modules. It allows
---writing of tests in the
---[TAP protocol](https://en.wikipedia.org/wiki/Test_Anything_Protocol).
---The results from the tests can be parsed by standard TAP-analyzers so they
---can be passed to utilities such as
---[prove](https://metacpan.org/pod/distribution/Test-Harness/bin/prove).
---Thus, one can run tests and then use the results for statistics,
---decision-making, and so on.
---
---**Example**
---
---To run this example: put the script in a file named `./tap.lua`, then make
---`tap.lua` executable by saying `chmod a+x ./tap.lua`, then execute using
---Tarantool as a script processor by saying `./tap.lua`.
---
---```lua
---#!/usr/bin/tarantool
---local tap = require('tap')
---test = tap.test("my test name")
---test:plan(2)
---test:ok(2 * 2 == 4, "2 * 2 is 4")
---test:test("some subtests for test2", function(test)
---     test:plan(2)
---     test:is(2 + 2, 4, "2 + 2 is 4")
---     test:isnt(2 + 3, 4, "2 + 3 is not 4")
---end)
---test:check()
---```
---
---The output from the above script will look approximately like this:
---
---```
---TAP version 13
---1..2
---ok - 2 * 2 is 4
---     # some subtests for test2
---     1..2
---     ok - 2 + 2 is 4
---     ok - 2 + 3 is not 4
---     # some subtests for test2: end
---ok - some subtests for test2
---```
---
---@class tap
---@field test fun(
---    name: string,
---    fun?: function,
---    ...
---): taptest Initialize.
---
---    The result of `tap.test` is an object, which is necessary for
---    `taptest:plan()` and all the other methods.
---
---    **Example:**
---
---```lua
---tap = require('tap')
---taptest = tap.test('test-name')
---```

---A test object returned by `tap.test()`. It has methods for performing
---tests, printing diagnostic messages, and planning the number of checks.
---
---@class taptest
---@field strict boolean Flag, true if comparisons with `nil` should be strict.
---
---    Set `taptest.strict=true` if `taptest:is()`, `taptest:isnt()` and
---    `taptest:is_deeply()` must be compared strictly with `nil`. Set
---    `taptest.strict=false` if `nil` and `box.NULL` both have the same
---    effect. The default is false.
---
---    For example, if and only if `taptest.strict=true` has happened, then
---    `taptest:is_deeply({a = box.NULL}, {})` will return `false`.
---
---    Since v. 2.8.3, `taptest.strict` is inherited in all subtests:
---
---```lua
---t = require('tap').test('123')
---t.strict = true
---
---t:is_deeply({a = box.NULL}, {}) -- false
---
---t:test('subtest', function(t)
---    t:is_deeply({a = box.NULL}, {}) -- also false
---end)
---```
---@field test fun(
---    self: taptest,
---    name: string,
---    fun?: function,
---    ...
---): taptest Create a subtest, or create a subtest, run the test
---    function and print the result.
---
---    If no `fun` argument is specified, create a subtest. If all arguments
---    are specified, create a subtest, run the test function and print the
---    result.
---
---    Parameters:
---
---    * `name` -- an arbitrary name to give for the test outputs.
---    * `fun` -- the test logic to run.
---    * `...` -- optional arguments passed to `fun`.
---
---    Returns the created subtest, or the result of `taptest:check()` if
---    `fun` is specified.
---@field plan fun(
---    self: taptest,
---    planned: number
---): nil Indicate how many tests will be performed.
---
---    Parameters:
---
---    * `planned` -- the number of tests to perform.
---@field check fun(
---    self: taptest
---): boolean Checks the number of tests performed.
---
---    The result will be a display saying `# bad plan: ...` if the number
---    of completed tests is not equal to the number of tests specified by
---    `taptest:plan(...)`. (This is a purely Tarantool feature: "bad plan"
---    messages are out of the TAP13 standard.)
---
---    This check should only be done after all planned tests are complete,
---    so ordinarily `taptest:check()` will only appear at the end of a
---    script. However, as a Tarantool extension, `taptest:check()` may
---    appear at the end of any subtest. Therefore there are three ways to
---    cause the check:
---
---    * by calling `taptest:check()` at the end of a script,
---    * by calling a function which ends with a call to `taptest:check()`,
---    * or by calling `taptest:test('...', subtest-function-name)` where
---      subtest-function-name does not need to end with `taptest:check()`
---      because it can be called after the subtest is complete.
---
---    Returns true if the number of completed tests equals the planned
---    number and no tests failed, otherwise false.
---@field diag fun(
---    self: taptest,
---    fmt: string,
---    ...
---): nil Display a diagnostic message.
---
---    Parameters:
---
---    * `fmt` -- the format string of the message to be displayed.
---    * `...` -- optional arguments for the format string.
---@field ok fun(
---    self: taptest,
---    cond: boolean,
---    message: string,
---    extra?: table
---): boolean This is a basic function which is used by other functions.
---
---    Depending on the value of `cond`, print 'ok' or 'not ok' along with
---    debugging information. Displays the message.
---
---    Parameters:
---
---    * `cond` -- an expression which is true or false.
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the condition is true, otherwise false.
---
---    **Example:**
---
---```
---tarantool> taptest:ok(true, 'x')
---ok - x
------
---- true
---...
---tarantool> tap = require('tap')
------
---...
---tarantool> taptest = tap.test('test-name')
---TAP version 13
------
---...
---tarantool> taptest:ok(1 + 1 == 2, 'X')
---ok - X
------
---- true
---...
---```
---@field fail fun(
---    self: taptest,
---    message: string,
---    extra?: table
---): boolean `taptest:fail('x')` is equivalent to `taptest:ok(false, 'x')`.
---
---    Displays the message.
---
---    Parameters:
---
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns false.
---@field skip fun(
---    self: taptest,
---    message: string,
---    extra?: table
---): nil `taptest:skip('x')` is equivalent to
---    `taptest:ok(true, 'x' .. '# skip')`.
---
---    Displays the message.
---
---    Parameters:
---
---    * `message` -- name of the test.
---    * `extra` -- extra information.
---
---    Returns nil.
---
---    **Example:**
---
---```
---tarantool> taptest:skip('message')
---ok - message # skip
------
---- true
---...
---```
---@field is fun(
---    self: taptest,
---    got: any,
---    expected: any,
---    message: string,
---    extra?: table
---): boolean Check whether the first argument equals the second argument.
---
---    Displays an extensive message if the result is false.
---
---    Parameters:
---
---    * `got` -- actual result.
---    * `expected` -- expected result.
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the arguments are equal, otherwise false.
---@field isnt fun(
---    self: taptest,
---    got: any,
---    unexpected: any,
---    message: string,
---    extra?: table
---): boolean This is the negation of `taptest:is()`.
---
---    Parameters:
---
---    * `got` -- actual result.
---    * `unexpected` -- unexpected result.
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the arguments are different, otherwise false.
---@field is_deeply fun(
---    self: taptest,
---    got: any,
---    expected: any,
---    message: string,
---    extra?: table
---): boolean Recursive version of `taptest:is(...)`.
---
---    It can be used to compare tables as well as scalar values.
---
---    Parameters:
---
---    * `got` -- actual result.
---    * `expected` -- expected result.
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the arguments are equal, otherwise false.
---@field like fun(
---    self: taptest,
---    got: any,
---    pattern: string,
---    message: string,
---    extra?: table
---): boolean Verify a string against a pattern. Ok if match is found.
---
---    **Example:**
---
---```lua
---test:like(tarantool.version, '^[1-9]', "version")
---```
---
---    Parameters:
---
---    * `got` -- actual result.
---    * `pattern` -- the pattern to search for.
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the pattern matches, otherwise false.
---@field unlike fun(
---    self: taptest,
---    got: any,
---    pattern: string,
---    message: string,
---    extra?: table
---): boolean This is the negation of `taptest:like()`.
---
---    Ok if no match is found.
---
---    Parameters:
---
---    * `got` -- actual result.
---    * `pattern` -- the pattern to search for.
---    * `message` -- name of the test.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the pattern does not match, otherwise false.
---@field isnil fun(
---    self: taptest,
---    v: any,
---    message: string,
---    extra?: table
---): boolean Test whether a value is nil.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field isnumber fun(
---    self: taptest,
---    v: any,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a number.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field isstring fun(
---    self: taptest,
---    v: any,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a string.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field istable fun(
---    self: taptest,
---    v: any,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a table.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field isboolean fun(
---    self: taptest,
---    v: any,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a boolean.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field isfunction fun(
---    self: taptest,
---    v: any,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a function.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field isudata fun(
---    self: taptest,
---    v: any,
---    utype: string,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a userdata with the specified
---    metatable type.
---
---    Displays a long message if the value is not of the specified type.
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `utype` -- type of data that a passed value should have.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.
---@field iscdata fun(
---    self: taptest,
---    v: any,
---    ctype: string,
---    message: string,
---    extra?: table
---): boolean Test whether a value is a cdata of the specified type.
---
---    Displays a long message if the value is not of the specified type.
---
---    **Example:**
---
---```lua
---test:iscdata(slab_info.quota_size, ffi.typeof('uint64_t'),
---    'memcached.slab.info().quota_size returns a cdata')
---```
---
---    Parameters:
---
---    * `v` -- value the type of which is to be checked.
---    * `ctype` -- type of data that a passed value should have.
---    * `message` -- text that will be shown to the user in case of failure.
---    * `extra` -- extra information to print in case of failure.
---
---    Returns true if the value has the specified type.

-- yaml formatter must be able to encode any Lua variable
local yaml = require('yaml').new()
yaml.cfg{
    encode_invalid_numbers = true;
    encode_load_metatables = true;
    encode_use_tostring    = true;
    encode_invalid_as_nil  = true;
}

local ffi = require('ffi') -- for iscdata

local function traceback(level)
    local trace = {}
    level = level or 3
    while true do
        local info = debug.getinfo(level, "nSl")
        if not info then break end
        local frame = {
            source = info.source;
            src = info.short_src;
            line = info.linedefined or 0;
            what = info.what;
            name = info.name;
            namewhat = info.namewhat;
            filename = info.source:sub(1, 1) == "@" and info.source:sub(2) or
                'eval'
        }
        table.insert(trace, frame)
        level = level + 1
    end
    return trace
end

---@param test taptest
---@param fmt string the format string of the message to be displayed.
---@param ... any optional arguments for the format string.
---@return nil
local function diag(test, fmt, ...)
    io.write(string.rep(' ', 4 * test.level), "# ", string.format(fmt, ...),
        "\n")
end

---@param test taptest
---@param cond boolean an expression which is true or false.
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the condition is true, otherwise false
local function ok(test, cond, message, extra)
    test.total = test.total + 1
    io.write(string.rep(' ', 4 * test.level))
    if cond then
        io.write(string.format("ok - %s\n", message))
        return true
    end

    test.failed = test.failed + 1
    io.write(string.format("not ok - %s\n", message))
    extra = extra or {}
    if test.trace then
        extra.trace = traceback()
        extra.filename = extra.trace[#extra.trace].filename
        extra.line = extra.trace[#extra.trace].line
    end
    if next(extra) == nil then
        return false -- don't have extra information
    end
    -- print aligned yaml output
    for line in yaml.encode(extra):gmatch("[^\n]+") do
        io.write(string.rep(' ', 2 + 4 * test.level), line, "\n")
    end
    return false
end

---@param test taptest
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result always false
local function fail(test, message, extra)
    return ok(test, false, message, extra)
end

---@param test taptest
---@param message string name of the test.
---@param extra? table extra information.
---@return nil
local function skip(test, message, extra)
    ok(test, true, message.." # skip", extra)
end

local function cmpdeeply(got, expected, extra)
    if type(expected) == "number" or type(got) == "number" then
        extra.got = got
        extra.expected = expected
        if got ~= got and expected ~= expected then
            return true -- nan
        end
        return got == expected
    end

    if ffi.istype('bool', got) then got = (got == 1) end
    if ffi.istype('bool', expected) then expected = (expected == 1) end

    if extra.strict and type(got) ~= type(expected) then
        extra.got = type(got)
        extra.expected = type(expected)
        return false
    end

    if type(got) ~= 'table' or type(expected) ~= 'table' then
        extra.got = got
        extra.expected = expected
        return got == expected
    end

    local path = extra.path or '/'
    local visited_keys = {}

    for i, v in pairs(got) do
        visited_keys[i] = true
        extra.path = path .. '/' .. i
        if not cmpdeeply(v, expected[i], extra) then
            return false
        end
    end

    -- check if expected contains more keys then got
    for i, v in pairs(expected) do
        if visited_keys[i] ~= true and (extra.strict or v ~= box.NULL) then
            extra.expected = 'key ' .. tostring(i)
            extra.got = 'nil'
            return false
        end
    end

    extra.path = path

    return true
end

---@param test taptest
---@param got any actual result.
---@param pattern string the pattern to search for.
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the pattern matches, otherwise false
local function like(test, got, pattern, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = pattern
    return ok(test, string.match(tostring(got), pattern) ~= nil, message, extra)
end

---@param test taptest
---@param got any actual result.
---@param pattern string the pattern to search for.
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the pattern does not match, otherwise false
local function unlike(test, got, pattern, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = pattern
    return ok(test, string.match(tostring(got), pattern) == nil, message, extra)
end

---@param test taptest
---@param got any actual result.
---@param expected any expected result.
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the arguments are equal, otherwise false
local function is(test, got, expected, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = expected
    local rc = (test.strict == false or type(got) == type(expected)) and
               got == expected
    return ok(test, rc, message, extra)
end

---@param test taptest
---@param got any actual result.
---@param unexpected any unexpected result.
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the arguments are different, otherwise false
local function isnt(test, got, unexpected, message, extra)
    extra = extra or {}
    extra.got = got
    extra.unexpected = unexpected
    local rc = (test.strict == true and type(got) ~= type(unexpected)) or
               got ~= unexpected
    return ok(test, rc, message, extra)
end


---@param test taptest
---@param got any actual result.
---@param expected any expected result.
---@param message string name of the test.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the arguments are equal, otherwise false
local function is_deeply(test, got, expected, message, extra)
    extra = extra or {}
    extra.got = got
    extra.expected = expected
    extra.strict = test.strict
    return ok(test, cmpdeeply(got, expected, extra), message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function isnil(test, v, message, extra)
    return is(test, not v and 'nil' or v, 'nil', message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function isnumber(test, v, message, extra)
    return is(test, type(v), 'number', message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function isstring(test, v, message, extra)
    return is(test, type(v), 'string', message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function istable(test, v, message, extra)
    return is(test, type(v), 'table', message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function isboolean(test, v, message, extra)
    return is(test, type(v), 'boolean', message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function isfunction(test, v, message, extra)
    return is(test, type(v), 'function', message, extra)
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param utype string type of data that a passed value should have.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function isudata(test, v, utype, message, extra)
    extra = extra or {}
    extra.expected = 'userdata<'..utype..'>'
    if type(v) == 'userdata' then
        extra.got = 'userdata<'..getmetatable(v)..'>'
        return ok(test, getmetatable(v) == utype, message, extra)
    else
        extra.got = type(v)
        return fail(test, message, extra)
    end
end

---@param test taptest
---@param v any value the type of which is to be checked.
---@param ctype string type of data that a passed value should have.
---@param message string text that will be shown to the user in case of
---    failure.
---@param extra? table extra information to print in case of failure.
---@return boolean result true if the value has the specified type
local function iscdata(test, v, ctype, message, extra)
    extra = extra or {}
    extra.expected = ffi.typeof(ctype)
    if type(v) == 'cdata' then
        extra.got = ffi.typeof(v)
        return ok(test, ffi.istype(ctype, v), message, extra)
    else
        extra.got = type(v)
        return fail(test, message, extra)
    end
end

local test_mt
---@param parent taptest? the parent test object, or nil for the root test.
---@param name string an arbitrary name to give for the test outputs.
---@param fun? function the test logic to run.
---@param ... any optional arguments passed to `fun`.
---@return taptest|boolean # the created subtest, or the result of
---    `taptest:check()` if `fun` is specified
local function test(parent, name, fun, ...)
    local level = parent ~= nil and parent.level + 1 or 0
    local test = setmetatable({
        parent  = parent;
        name    = name;
        level   = level;
        total   = 0;
        failed  = 0;
        planned = 0;
        trace   = parent == nil and true or parent.trace;
        strict = parent ~= nil and parent.strict or false;
    }, test_mt)
    if fun ~= nil then
        test:diag('%s', test.name)
        fun(test, ...)
        test:diag('%s: end', test.name)
        return test:check()
    else
        return test
    end
end

---@param test taptest
---@param planned number the number of tests to perform.
---@return nil
local function plan(test, planned)
    test.planned = planned
    io.write(string.rep(' ', 4 * test.level), string.format("1..%d\n", planned))
end

---@param test taptest
---@return boolean result true if the number of completed tests equals the
---    planned number and no tests failed, otherwise false
local function check(test)
    if test.checked then
        error('check called twice')
    end
    test.checked = true
    if test.planned ~= test.total then
        if test.parent ~= nil then
            ok(test.parent, false, "bad plan", { planned = test.planned;
                run = test.total})
        else
            diag(test, string.format("bad plan: planned %d run %d",
                test.planned, test.total))
        end
    elseif test.failed > 0 then
        if test.parent ~= nil then
            ok(test.parent, false, "failed subtests", {
                failed = test.failed;
                planned = test.planned;
            })
        else
            diag(test, "failed subtest: %d", test.failed)
        end
    else
        if test.parent ~= nil then
            ok(test.parent, true, test.name)
        end
    end
    return test.planned == test.total and test.failed == 0
end

test_mt = {
    __index = {
        test      = test;
        plan      = plan;
        check     = check;
        diag      = diag;
        ok        = ok;
        fail      = fail;
        skip      = skip;
        is        = is;
        isnt      = isnt;
        isnil     = isnil;
        isnumber  = isnumber;
        isstring  = isstring;
        istable   = istable;
        isboolean = isboolean;
        isfunction = isfunction;
        isudata   = isudata;
        iscdata   = iscdata;
        is_deeply = is_deeply;
        like      = like;
        unlike    = unlike;
    }
}

---@param ... any optional arguments passed to the test function.
---@return taptest|boolean
local function root_test(...)
    io.write('TAP version 13', '\n')
    return test(nil, ...)
end

return {
    test = root_test;
}
