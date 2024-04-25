local t = require('luatest')

local g = t.group()

local TEST_FILE = debug.getinfo(1, 'S').short_src

g.test_invalid_args = function()
    local arg, errmsg

    arg = {type = 'foo', message = 'bar'}

    -- Raising a new error with a bad level argument.
    errmsg = 'box.error(): bad arguments'
    t.assert_error_msg_equals(errmsg, box.error, arg, {})
    t.assert_error_msg_equals(errmsg, box.error, arg, 'x')
    t.assert_error_msg_equals(errmsg, box.error, arg, '1')
    t.assert_error_msg_equals(errmsg, box.error, arg, 1, 2)

    -- Creating a new error with a bad level argument.
    errmsg = 'box.error.new(): bad arguments'
    t.assert_error_msg_equals(errmsg, box.error.new, arg, {})
    t.assert_error_msg_equals(errmsg, box.error.new, arg, 'x')
    t.assert_error_msg_equals(errmsg, box.error.new, arg, '1')
    t.assert_error_msg_equals(errmsg, box.error.new, arg, 1, 2)

    arg = box.error.new(arg)

    -- Raising an existing error with a bad level argument.
    errmsg = 'box.error(): bad arguments'
    t.assert_error_msg_equals(errmsg, box.error, arg, {})
    t.assert_error_msg_equals(errmsg, box.error, arg, 'x')
    t.assert_error_msg_equals(errmsg, box.error, arg, '1')
    t.assert_error_msg_equals(errmsg, box.error, arg, 1, 2)

    -- Sic: box.error.new() doesn't take an error object.
    errmsg = 'box.error.new(): bad arguments'
    t.assert_error_msg_equals(errmsg, box.error.new, arg)
    t.assert_error_msg_equals(errmsg, box.error.new, arg, 1)
    t.assert_error_msg_equals(errmsg, box.error.new, arg, {})
    t.assert_error_msg_equals(errmsg, box.error.new, arg, 'x')
    t.assert_error_msg_equals(errmsg, box.error.new, arg, '1')
    t.assert_error_msg_equals(errmsg, box.error.new, arg, 1, 2)

    local errcode = box.error.TEST_TYPE_INT
    -- Raising a new code error with a bad level argument.
    errmsg = 'box.error(): bad arguments'
    t.assert_error_msg_equals(errmsg, box.error, errcode, 1, 'x')
    t.assert_error_msg_equals(errmsg, box.error, errcode, 1, '1')

    -- Creating a new code error with a bad level argument.
    errmsg = 'box.error.new(): bad arguments'
    t.assert_error_msg_equals(errmsg, box.error.new, errcode, 1, 'x')
    t.assert_error_msg_equals(errmsg, box.error.new, errcode, 1, '1')
end

g.test_raise = function()
    local line1 = debug.getinfo(1, 'l').currentline + 3
    t.assert_gt(line1, 0)
    local function func1(...)
        box.error(...)
    end

    local line2 = debug.getinfo(1, 'l').currentline + 3
    t.assert_gt(line2, line1)
    local function func2(...)
        func1(...)
    end

    local line3 = debug.getinfo(1, 'l').currentline + 3
    t.assert_gt(line3, line2)
    local function func3(...)
        func2(...)
    end

    local function check_trace(func, line, ...)
        local ok, err = pcall(func, ...)
        t.assert_not(ok)
        t.assert_type(err, 'cdata')
        t.assert_is_not(err.trace[1], nil)
        t.assert_str_contains(err.trace[1].file, TEST_FILE)
        t.assert_equals(err.trace[1].line, line)
    end

    local function check_no_trace(func, ...)
        local ok, err = pcall(func, ...)
        t.assert_not(ok)
        t.assert_type(err, 'cdata')
        t.assert_equals(err.trace, {})
    end

    -- Raising a new error.
    local err = {type = 'TestError', message = 'Test'}

    check_trace(func1, line1, err)
    check_trace(func1, line1, err, nil)
    check_trace(func2, line1, err)
    check_trace(func2, line1, err, nil)
    check_trace(func3, line1, err)
    check_trace(func3, line1, err, nil)

    check_no_trace(func1, err, 0)
    check_no_trace(func2, err, 0)
    check_no_trace(func3, err, 0)

    check_trace(func1, line1, err, 1)
    check_trace(func2, line1, err, 1)
    check_trace(func3, line1, err, 1)

    check_trace(func2, line2, err, 2)
    check_trace(func3, line2, err, 2)

    check_trace(func3, line3, err, 3)

    -- Raising an existing error.
    local line = debug.getinfo(1, 'l').currentline + 2
    t.assert_gt(line, line3)
    err = box.error.new{type = 'TestError', message = 'Test'}

    check_trace(func1, line, err)
    check_trace(func1, line, err, nil)
    check_trace(func2, line, err)
    check_trace(func2, line, err, nil)
    check_trace(func3, line, err)
    check_trace(func3, line, err, nil)

    check_no_trace(func1, err, 0)
    check_no_trace(func2, err, 0)
    check_no_trace(func3, err, 0)

    check_trace(func1, line1, err, 1)
    check_trace(func2, line1, err, 1)
    check_trace(func3, line1, err, 1)

    check_trace(func2, line2, err, 2)
    check_trace(func3, line2, err, 2)

    check_trace(func3, line3, err, 3)

    -- Raising an errcode error.
    local errcode = box.error.ILLEGAL_PARAMS
    check_trace(func1, line1, errcode, 'foo')
    check_trace(func1, line1, errcode, 'foo', nil)
    check_trace(func2, line1, errcode, 'foo')
    check_trace(func2, line1, errcode, 'foo', nil)
    check_trace(func3, line1, errcode, 'foo')
    check_trace(func3, line1, errcode, 'foo', nil)

    check_no_trace(func1, errcode, 'foo', 0)
    check_no_trace(func2, errcode, 'foo', 0)
    check_no_trace(func3, errcode, 'foo', 0)

    check_trace(func1, line1, errcode, 'foo', 1)
    check_trace(func2, line1, errcode, 'foo', 1)
    check_trace(func3, line1, errcode, 'foo', 1)

    check_trace(func2, line2, errcode, 'foo', 2)
    check_trace(func3, line2, errcode, 'foo', 2)

    check_trace(func3, line3, errcode, 'foo', 3)
end

g.test_new = function()
    local line1 = debug.getinfo(1, 'l').currentline + 3
    t.assert_gt(line1, 0)
    local function func1(...)
        local err = box.error.new(...)
        return err
    end

    local line2 = debug.getinfo(1, 'l').currentline + 3
    t.assert_gt(line2, line1)
    local function func2(...)
        local err = func1(...)
        return err
    end

    local line3 = debug.getinfo(1, 'l').currentline + 3
    t.assert_gt(line3, line2)
    local function func3(...)
        local err = func2(...)
        return err
    end

    local function check_trace(func, line, ...)
        local err = func(...)
        t.assert_type(err, 'cdata')
        t.assert_is_not(err.trace[1], nil)
        t.assert_str_contains(err.trace[1].file, TEST_FILE)
        t.assert_equals(err.trace[1].line, line)
    end

    local function check_no_trace(func, ...)
        local err = func(...)
        t.assert_type(err, 'cdata')
        t.assert_equals(err.trace, {})
    end

    local err = {type = 'TestError', message = 'Test'}

    check_trace(func1, line1, err)
    check_trace(func1, line1, err, nil)
    check_trace(func2, line1, err)
    check_trace(func2, line1, err, nil)
    check_trace(func3, line1, err)
    check_trace(func3, line1, err, nil)

    check_no_trace(func1, err, 0)
    check_no_trace(func2, err, 0)
    check_no_trace(func3, err, 0)

    check_trace(func1, line1, err, 1)
    check_trace(func2, line1, err, 1)
    check_trace(func3, line1, err, 1)

    check_trace(func2, line2, err, 2)
    check_trace(func3, line2, err, 2)

    check_trace(func3, line3, err, 3)

    local errcode = box.error.ILLEGAL_PARAMS
    check_trace(func1, line1, errcode, 'foo')
    check_trace(func1, line1, errcode, 'foo', nil)
    check_trace(func2, line1, errcode, 'foo')
    check_trace(func2, line1, errcode, 'foo', nil)
    check_trace(func3, line1, errcode, 'foo')
    check_trace(func3, line1, errcode, 'foo', nil)

    check_no_trace(func1, errcode, 'foo', 0)
    check_no_trace(func2, errcode, 'foo', 0)
    check_no_trace(func3, errcode, 'foo', 0)

    check_trace(func1, line1, errcode, 'foo', 1)
    check_trace(func2, line1, errcode, 'foo', 1)
    check_trace(func3, line1, errcode, 'foo', 1)

    check_trace(func2, line2, errcode, 'foo', 2)
    check_trace(func3, line2, errcode, 'foo', 2)

    check_trace(func3, line3, errcode, 'foo', 3)
end
