local utils = require('internal.utils')
local t = require('luatest')

local g = t.group()

g.test_check_param = function()
    t.assert_error_msg_equals("Illegal parameters, foo should be a string",
                              utils.check_param, 42, "foo", 'string')
    t.assert_error_msg_equals("Illegal parameters, foo should be a number",
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
    t.assert_error_msg_equals("Illegal parameters, options should be a table",
                              utils.check_param_table, 'foo', opts)
    t.assert_error_msg_equals("Illegal parameters, unexpected option 'foo'",
                              utils.check_param_table, {foo = 'bar'}, opts)
    t.assert_error_msg_equals("Illegal parameters, options parameter " ..
                              "'tuple_opt' should be of type tuple",
                              utils.check_param_table, {tuple_opt = {}}, opts)
    t.assert_error_msg_equals("Illegal parameters, options parameter " ..
                              "'str_opt' should be of type string",
                              utils.check_param_table, {str_opt = 42}, opts)
    t.assert_error_msg_equals("Illegal parameters, options parameter " ..
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
