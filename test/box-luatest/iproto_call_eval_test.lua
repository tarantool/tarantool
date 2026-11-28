local json = require('json')
local net_box = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        -- The Python client did not negotiate tuple extensions and received
        -- plain arrays for tuples returned by CALL and EVAL.
        require('compat').box_tuple_extension = 'old'
        box.schema.user.create('test', {password = 'test'})
        box.schema.user.grant('test', 'execute,read,write', 'universe')
    end)
    cg.conn = net_box.connect(cg.server.net_box_uri, {
        user = 'test', password = 'test', fetch_schema = false,
    })
    t.assert_equals(cg.conn:call('box.session.user'), 'test')
end)

g.after_all(function(cg)
    if cg.conn then
        cg.conn:close()
    end
    cg.server:drop()
end)

-- assert_equals ignores array/map hints and compares tuples as tables.
local function check_table_kinds(actual, expected, request)
    if type(expected) ~= 'table' then
        return
    end
    t.assert_type(actual, 'table', request)
    local kind = type(next(expected)) == 'string' and 'map' or 'seq'
    t.assert_equals((getmetatable(actual) or {}).__serialize, kind, request)
    for key, value in pairs(expected) do
        check_table_kinds(actual[key], value, request)
    end
end

local function check_response(cg, method, request, expected, args, context)
    local opts = {return_raw = true}
    local response = cg.conn[method](cg.conn, request, args, opts)
    local data = response:decode()
    local message = method:upper() .. ': ' .. (context or request)
    if args ~= nil then
        message = message .. ' (args: ' .. json.encode(args) .. ')'
    end
    t.assert_equals(data, expected, message)
    check_table_kinds(data, expected, message)
end

local function check_call(cg, name, expected, args)
    check_response(cg, 'call', name, expected, args)
end

local function define_return(cg, expr)
    cg.server:eval('function test_return(...) return ' .. expr .. ' end')
end

local function check_return_call(cg, expr, expected, args)
    define_return(cg, expr)
    check_response(cg, 'call', 'test_return', expected, args, 'return ' .. expr)
end

g.test_call_resolution_and_errors = function(cg)
    -- A trailing Lua nil occupies a slot in the CALL response.
    cg.server:exec(function()
        local exp_notation = 1e123
        local function mixed_return()
            return 'testing', 1, false, -1, 1.123,
                   math.abs(exp_notation - 1e123) < 0.1, nil
        end
        rawset(_G, 'mixed_return', mixed_return)
        local function pack(...)
            return {n = select('#', ...), ...}
        end
        t.assert_equals(pack(mixed_return()),
                        {'testing', 1, false, -1, 1.123, true, n = 7})
        local function lookup() return 'OK' end
        rawset(_G, 'lookup', lookup)
        rawset(_G, 'nested', {lookup = lookup, nested = {lookup = lookup}})
    end)
    check_call(cg, 'mixed_return', {'testing', 1, false, -1, 1.123, true,
                                   box.NULL})
    cg.server:exec(function() rawset(_G, 'mixed_return', nil) end)
    t.assert_error_covers({code = box.error.NO_SUCH_PROC,
                           message = "Procedure 'mixed_return' is not defined"},
                          cg.conn.call, cg.conn, 'mixed_return')
    cg.server:exec(function()
        rawset(_G, 'mixed_return', function() return _G.mixed_return end)
    end)
    t.assert_error_covers({code = box.error.PROC_LUA,
                           message = "unsupported Lua type 'function'"},
                          cg.conn.call, cg.conn, 'mixed_return')
    t.assert_error_covers({code = 33333, message = 'Unknown error'},
                          cg.conn.call, cg.conn, 'box.error', {33333})

    local names = {'lookup', 'nested.lookup', 'nested.nested.lookup'}
    for _, name in ipairs(names) do
        check_call(cg, name, {'OK'})
    end
end

g.test_call_returns = function(cg)
    -- Packing varargs adds an array level even when there are no arguments.
    cg.server:exec(function()
        local function array_return(...) return {...} end
        rawset(_G, 'array_return', array_return)
        rawset(_G, 'nested_return', function(...)
            return array_return({...})
        end)
    end)
    local args = {'test_', 'test_'}
    check_call(cg, 'array_return', {args}, args)
    check_call(cg, 'nested_return', {{args}}, args)
    check_call(cg, 'array_return', {{}})
    check_call(cg, 'nested_return', {{{}}})
    check_call(cg, 'array_return', {{'jason'}}, {'jason'})
    args = {'jason', 1, 'test', 2, 'stewart'}
    check_call(cg, 'array_return', {args}, args)

    local cases = {
        {"1, 2, '1', '2'", {1, 2, '1', '2'}},
        {"{{'hello'}, {'world'}}", {{{'hello'}, {'world'}}}},
        {"{'hello', {'world'}}", {{'hello', {'world'}}}},
        {"'hello', {{'world'}, {'canada'}}",
         {'hello', {{'world'}, {'canada'}}}},
        {"{}, '123', {{}, {}}", {{}, '123', {{}, {}}}},
        {"{{{'hello'}}}", {{{{'hello'}}}}},
        {"{box.tuple.new('hello'), {'world'}}", {{{'hello'}, {'world'}}}},
        {"{{'world'}, box.tuple.new('hello')}", {{{'world'}, {'hello'}}}},
        {'{{test = {1, 2, 3}}, {test2 = {1, 2, 3}}}',
         {{{test = {1, 2, 3}}, {test2 = {1, 2, 3}}}}},
    }
    for _, case in ipairs(cases) do
        check_return_call(cg, unpack(case))
    end
end

g.test_call_dml = function(cg)
    cg.server:exec(function()
        local space = box.schema.space.create('tweedledum')
        space:create_index('primary', {type = 'hash'})
        rawset(_G, 'dml_space', space)
        rawset(_G, 'myinsert', function(...) return space:insert{...} end)
        rawset(_G, 'myreplace', function(...) return space:replace{...} end)
        rawset(_G, 'field_x', function(key, field)
            return space:get(key)[field]
        end)
    end)
    local function check_get_select(key, row)
        local data = row and {row} or {}
        check_call(cg, 'dml_space:get', data, {key})
        check_call(cg, 'dml_space:select', {data}, {key})
        cg.server:exec(function(key, data)
            local space = _G.dml_space
            t.assert_equals(space:get{key}, data[1])
            t.assert_equals(space:select{key}, data)
        end, {key, data})
    end

    local row = {1, 'test box delete'}
    check_call(cg, 'myinsert', {row}, row)
    check_call(cg, 'dml_space:delete', {row}, {1})
    check_call(cg, 'dml_space:delete', {}, {1})
    row = {2, 'test box delete'}
    check_call(cg, 'myinsert', {row}, row)
    check_call(cg, 'dml_space:delete', {}, {1})
    check_call(cg, 'dml_space:delete', {row}, {2})
    check_call(cg, 'dml_space:delete', {}, {2})
    cg.server:exec(function()
        t.assert_equals(_G.dml_space:delete{2}, nil)
    end)

    check_call(cg, 'myinsert', {row}, row)
    check_call(cg, 'dml_space:get', {row}, {2})
    cg.server:exec(function()
        t.assert_equals(_G.dml_space:delete{2}, {2, 'test box delete'})
    end)
    check_call(cg, 'dml_space:get', {}, {2})

    row = {2, 'test box.select()'}
    check_call(cg, 'myinsert', {row}, row)
    check_get_select(2, row)
    check_get_select(1)

    local replacements = {
        {2, 'hello', 'world'}, {2, 'goodbye', 'universe'}, {2},
    }
    for _, replacement in ipairs(replacements) do
        check_call(cg, 'myreplace', {replacement}, replacement)
        check_get_select(2, replacement)
    end
    check_call(cg, 'dml_space:delete', {{2}}, {2})
    check_call(cg, 'dml_space:delete', {}, {2})

    row = {3, 'old', 2}
    check_call(cg, 'myinsert', {row}, row)
    t.assert_error_covers({
        code = box.error.TUPLE_FOUND,
        message = 'Duplicate key exists in unique index "primary" in space ' ..
                  '"tweedledum" with old tuple - [3, "old", 2] and new ' ..
                  'tuple - [3, "old", 2]',
    }, cg.conn.call, cg.conn, 'myinsert', row)

    -- A primary key change needs a tuple copy, not an in-place update.
    cg.server:exec(function()
        local space = _G.dml_space
        local ops = {{'=', 1, 4}, {'=', 2, 'new'}}
        t.assert_error_covers({
            code = box.error.CANT_UPDATE_PRIMARY_KEY,
            message = 'Attempt to modify a tuple field which is part of ' ..
                      "primary index in space 'tweedledum'",
        }, space.update, space, {3}, ops)
        space:insert(space:get{3}:update(ops))
        space:delete{3}
    end)
    check_get_select(4, {4, 'new', 2})
    cg.server:exec(function()
        local space = _G.dml_space
        t.assert_equals(space:update({4}, {{'+', 3, 1}}), {4, 'new', 3})
        t.assert_equals(space:update({4}, {{'-', 3, 1}}), {4, 'new', 2})
    end)
    check_get_select(4, {4, 'new', 2})
    check_call(cg, 'field_x', {4}, {4, 1})
    check_call(cg, 'field_x', {'new'}, {4, 2})
    check_call(cg, 'dml_space:delete', {{4, 'new', 2}}, {4})
end

local function check_call_and_eval(cg, expr, expected, args)
    check_response(cg, 'eval', 'return ' .. expr, expected, args)
    check_return_call(cg, expr, expected, args)
end

g.test_return_arity = function(cg)
    -- CALL and EVAL distinguish nil, no results and an empty array.
    local cases = {
        {'1', {1}},
        {'1, 2, 3', {1, 2, 3}},
        {'true', {true}},
        {'nil', {box.NULL}},
        {'', {}},
        {'{}', {{}}},
        {'{1}', {{1}}},
        {'{1, 2, 3}', {{1, 2, 3}}},
        {"require('fiber').sleep(0)", {}},
    }
    for _, case in ipairs(cases) do
        check_call_and_eval(cg, unpack(case))
    end
end

g.test_nested_maps_and_tuples = function(cg)
    cg.server:exec(function()
        local tuple = box.tuple.new{'tuple', {1, 2, 3}, {k1 = 'v', k2 = 'v2'}}
        rawset(_G, 'test_tuple', tuple)
    end)
    local map = {k1 = 'v1', k2 = 'v2'}
    local map_expr = [[{
        s = {1, 1428578535}, u = 1428578535, v = {},
        c = {['2'] = {1, 1428578535}, ['106'] = {1, 1428578535}},
        pc = {['2'] = {1, 1428578535, 9243}, ['106'] = {1, 1428578535, 9243}},
    }]]
    local nested_map = assert(loadstring('return ' .. map_expr))()
    local tuple = {'tuple', {1, 2, 3}, {k1 = 'v', k2 = 'v2'}}
    -- Preserve the distinction between arrays and maps inside nested data.
    local cases = {
        {"{k1 = 'v1', k2 = 'v2'}", {map}},
        {map_expr, {nested_map}},
        {'true, ' .. map_expr, {true, nested_map}},
        {map_expr .. ', true', {nested_map, true}},
        {'test_tuple', {tuple}},
        {'test_tuple, test_tuple, test_tuple', {tuple, tuple, tuple}},
        {'{test_tuple}', {{tuple}}},
        {'{test_tuple, test_tuple, test_tuple}', {{tuple, tuple, tuple}}},
    }
    for _, case in ipairs(cases) do
        check_call_and_eval(cg, unpack(case))
    end
end

g.test_echo_arguments = function(cg)
    check_call_and_eval(cg, '...', {})
    local cases = {
        {1, 2, 3},
        {box.NULL, box.NULL, box.NULL},
        {{k1 = 'v1', k2 = 'v2'}},
    }
    for _, args in ipairs(cases) do
        check_call_and_eval(cg, '...', args, args)
    end
end

g.test_call_and_eval_errors = function(cg)
    local errors = {
        {"error('exception')", {code = box.error.PROC_LUA,
                                 message = 'exception'}},
        -- The Python client's masked return code was 0, so it printed no
        -- data for this error response, just as it did for an empty result.
        {'box.error(0)', {name = 'UNKNOWN', message = 'Unknown error'}},
    }
    for _, case in ipairs(errors) do
        local expr, err = unpack(case)
        t.assert_error_covers(err, cg.conn.eval, cg.conn, 'return ' .. expr)
        define_return(cg, expr)
        t.assert_error_covers(err, cg.conn.call, cg.conn, 'test_return')
    end
    t.assert_error_covers({code = box.error.PROC_LUA,
                           message = "eval:1: unexpected symbol near '!'"},
                          cg.conn.eval, cg.conn, '!invalid expression')
end

g.test_call_and_eval_transactions = function(cg)
    cg.server:exec(function()
        local space = box.schema.space.create('transaction_space')
        space:create_index('primary', {type = 'tree'})
        rawset(_G, 'transaction_space', space)
    end)
    local insert = "transaction_space:auto_increment({'transaction'})"
    check_response(cg, 'eval', 'return ' .. insert, {{1, 'transaction'}})
    check_return_call(cg, insert, {{2, 'transaction'}})

    -- The rolled-back insert returns a tuple without leaving a row behind.
    local rows = {{{1, 'transaction'}, {2, 'transaction'}}}
    check_call_and_eval(cg, 'transaction_space:select{}', rows)
    check_call_and_eval(cg, "box.begin(), " ..
                        "transaction_space:auto_increment({'failed'}), " ..
                        'box.rollback()', {box.NULL, {3, 'failed'}})
    check_call_and_eval(cg, 'transaction_space:select{}', rows)
end
