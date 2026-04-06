local t = require('luatest')
local server = require('luatest.server')

local g1 = t.group('iproto_export', t.helpers.matrix({
    before_box_cfg = {false, true},
}))

g1.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g1.test_iproto_export = function(cg)
    local test_code = [[
        box.iproto.export('test.func_1', function(v) return {1, v} end)
        box.iproto.export('test.func_2', function(v) return {2, v} end)
        rawset(_G, 'test', {})
        _G.test.func_2 = function(v) return {123, v} end
        _G.test.func_3 = function(v) return {3, v} end
    ]]
    local env = {}
    if cg.params.before_box_cfg then
        env.TARANTOOL_RUN_BEFORE_BOX_CFG = test_code
    end
    cg.server = server:new({env = env})
    cg.server:start()
    if not cg.params.before_box_cfg then
        cg.server:eval(test_code)
    end
    t.assert_equals(cg.server:call('test.func_1', {'x'}), {1, 'x'})
    t.assert_equals(cg.server:call('test.func_2', {'x'}), {2, 'x'})
    t.assert_equals(cg.server:call('test.func_3', {'x'}), {3, 'x'})
    t.assert_error_covers({
        type = 'ClientError',
        name = 'NO_SUCH_PROC',
        func = 'test.func_4',
    }, cg.server.call, cg.server, 'test.func_4', {'x'})
end

local g2 = t.group('iproto_export')

g2.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g2.test_invalid_arguments = function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function()
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'function name should be a string',
        }, box.iproto.export, 10, 20)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'function object should be a function',
        }, box.iproto.export, 'foo', 20)
        box.iproto.export('foo', function() end)
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNCTION_EXISTS',
            func = 'foo',
        }, box.iproto.export, 'foo', function() end)
    end)
end

g2.test_iproto_export_in_app_threads = function(cg)
    cg.server = server:new({box_cfg = {app_threads = 2}})
    cg.server:start()
    cg.server:call('box.iproto.internal.enable_thread_requests')
    cg.server:exec(function()
        box.iproto.export('test.func_1', function(v) return {1, v} end)
    end, {}, {_thread_id = 0})
    cg.server:exec(function()
        box.iproto.export('test.func_2', function(v) return {2, v} end)
        rawset(_G, 'test', {})
        _G.test.func_2 = function(v) return {123, v} end
    end, {}, {_thread_id = 1})
    cg.server:exec(function()
        rawset(_G, 'test', {})
        _G.test.func_3 = function(v) return {3, v} end
    end, {}, {_thread_id = 2})
    local err = {type = 'ClientError', name = 'NO_SUCH_PROC'}
    t.assert_equals(cg.server:call('test.func_1', {'x'}, {_thread_id = 0}),
                    {1, 'x'})
    t.assert_error_covers(err, cg.server.call, cg.server,
                          'test.func_2', {'x'}, {_thread_id = 0})
    t.assert_error_covers(err, cg.server.call, cg.server,
                          'test.func_3', {'x'}, {_thread_id = 0})
    t.assert_equals(cg.server:call('test.func_2', {'x'}, {_thread_id = 1}),
                    {2, 'x'})
    t.assert_error_covers(err, cg.server.call, cg.server,
                          'test.func_1', {'x'}, {_thread_id = 1})
    t.assert_error_covers(err, cg.server.call, cg.server,
                          'test.func_3', {'x'}, {_thread_id = 1})
    t.assert_equals(cg.server:call('test.func_3', {'x'}, {_thread_id = 2}),
                    {3, 'x'})
    t.assert_error_covers(err, cg.server.call, cg.server,
                          'test.func_1', {'x'}, {_thread_id = 2})
    t.assert_error_covers(err, cg.server.call, cg.server,
                          'test.func_2', {'x'}, {_thread_id = 2})
end
