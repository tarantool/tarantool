local utils = require('internal.utils')
local t = require('luatest')

local g = t.group()

g.test_check_param = function()
    t.assert_error_msg_equals("foo should be a string",
                              utils.check_param, 42, "foo", 'string')
    t.assert_error_msg_equals("foo should be a number",
                              utils.check_param, '42', "foo", 'number')
    t.assert(pcall(utils.check_param, '42', 'foo', 'string'))
    t.assert(pcall(utils.check_param, 42, 'foo', 'number'))
    t.assert(pcall(utils.check_param, 123456789123456789ULL, 'foo', 'number'))
end

g.test_check_param_table = function()
    local opts = {
        any_opt = 'any',
        str_opt = 'string',
        str_num_opt = 'string,number',
        tuple_opt = function(opt)
            return box.tuple.is(opt), 'tuple'
        end
    }
    t.assert_error_msg_equals("options should be a table",
                              utils.check_param_table, 'foo', opts)
    t.assert_error_msg_equals("unexpected option 'foo'",
                              utils.check_param_table, {foo = 'bar'}, opts)
    t.assert_error_msg_equals("options parameter " ..
                              "'tuple_opt' should be of type tuple",
                              utils.check_param_table, {tuple_opt = {}}, opts)
    t.assert_error_msg_equals("options parameter " ..
                              "'str_opt' should be of type string",
                              utils.check_param_table, {str_opt = 42}, opts)
    t.assert_error_msg_equals("options parameter " ..
                              "'str_num_opt' should be one of types: " ..
                              "string,number",
                              utils.check_param_table, {str_num_opt = {}}, opts)
    t.assert(pcall(utils.check_param_table, nil, opts))
    t.assert(pcall(utils.check_param_table, {}, opts))
    t.assert(pcall(utils.check_param_table, {
        any_opt = 'foo',
        str_opt = 'bar',
        str_num_opt = 42,
        tuple_opt = box.tuple.new(),
    }, opts))
    t.assert(pcall(utils.check_param_table, {
        any_opt = {},
        str_num_opt = 'foo',
    }, opts))
end

g.test_update_param_table = function()
    local opts = {
        a = 'foo',
        b = 'bar',
    }
    local defaults = {
        b = 'x',
        c = 'y',
    }
    t.assert_equals(utils.update_param_table(opts, defaults), {
        a = 'foo',
        b = 'bar',
        c = 'y',
    })
end

g.test_is_callable = function()
    t.assert(utils.is_callable(function() end))
    local functor = setmetatable({}, {
        __call = function() end
    })
    t.assert(utils.is_callable(functor))
    t.assert_not(utils.is_callable(1))
    t.assert_not(utils.is_callable("foo"))
end

g.test_table_pack = function()
    t.assert_equals(utils.table_pack(), {n = 0})
    t.assert_equals(utils.table_pack(1), {n = 1, 1})
    t.assert_equals(utils.table_pack(1, 2), {n = 2, 1, 2})
    t.assert_equals(utils.table_pack(1, 2, nil), {n = 3, 1, 2})
    t.assert_equals(utils.table_pack(1, 2, nil, 3), {n = 4, 1, 2, nil, 3})
end

g.test_call_at = function()
    local f = function(...) return ... end
    t.assert_equals(utils.call_at(1, f, nil))
    t.assert_equals(utils.call_at(1, f, 1), 1)
    t.assert_equals(utils.table_pack(utils.call_at(1, f, 1, 2)),
                    utils.table_pack(1, 2))
    t.assert_equals(utils.table_pack(utils.call_at(1, f, 1, 2, nil)),
                    utils.table_pack(1, 2, nil))
    t.assert_equals(utils.table_pack(utils.call_at(1, f, 1, 2, nil, 3)),
                    utils.table_pack(1, 2, nil, 3))

    local this_file = debug.getinfo(1, 'S').short_src
    local line1 = debug.getinfo(1, 'l').currentline + 1
    local f1 = function() box.error(box.error.UNKNOWN, 1) end
    local line2 = debug.getinfo(1, 'l').currentline + 1
    local f2 = function(level, ...) utils.call_at(level, f1, ...) end
    local line3 = debug.getinfo(1, 'l').currentline + 1
    local f3 = function(...) f2(...) end

    local check_box_error = function(level, line)
        local ok, err = pcall(f3, level)
        t.assert_not(ok)
        t.assert(box.error.is(err))
        t.assert_equals(err.trace[1].file, this_file)
        t.assert_equals(err.trace[1].line, line)
    end

    check_box_error(nil, line1)
    check_box_error(1, line2)
    check_box_error(2, line3)

    line1 = debug.getinfo(1, 'l').currentline + 1
    f1 = function() error('foo', 1) end

    local check_box_error = function(level, line_src, line_level)
        local ok, err = pcall(f3, level)
        t.assert_not(ok)
        t.assert_equals(tostring(err),
                        string.format('%s:%s: %s:%s: foo',
                        this_file, line_level, this_file, line_src))
    end

    check_box_error(1, line1, line2)
    check_box_error(2, line1, line3)
end
