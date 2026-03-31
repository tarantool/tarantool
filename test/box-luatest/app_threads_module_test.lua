local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')

local g = t.group()

g.test_threads_config_propagation = function()
    --
    -- Check that the threads configuration is propagated to box.cfg
    -- and the threads module.
    --
    local config = cbuilder:new()
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 1},
            {name = 'test2', size = 2},
            {name = 'test3', size = 3},
        })
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    local expected_threads_info = {
        thread_id = 0,
        group_name = 'tx',
        groups = {
            {name = 'tx', size = 1},
            {name = 'test1', size = 1},
            {name = 'test2', size = 2},
            {name = 'test3', size = 3},
        },
    }
    cluster.server:exec(function(expected_threads_info)
        t.assert_equals(box.cfg.app_threads, 6)
        t.assert_equals(require('experimental.threads').info(),
                        expected_threads_info)
    end, {expected_threads_info})
    --
    -- Check that the threads configuration is updated only after
    -- instance restart.
    --
    config = cbuilder:new()
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 1},
            {name = 'test2', size = 2},
            {name = 'test3', size = 3},
            {name = 'test4', size = 4},
        })
        :config()
    cluster:reload(config)
    cluster.server:exec(function(expected_threads_info)
        t.assert_covers(require('config'):info().alerts[1], {
            message = 'box_cfg.apply: threads configuration ' ..
                      'will not be set until the instance is restarted',
        })
        t.assert_equals(box.cfg.app_threads, 6)
        t.assert_equals(require('experimental.threads').info(),
                        expected_threads_info)
    end, {expected_threads_info})
    cluster.server:restart()
    local expected_threads_info = {
        thread_id = 0,
        group_name = 'tx',
        groups = {
            {name = 'tx', size = 1},
            {name = 'test1', size = 1},
            {name = 'test2', size = 2},
            {name = 'test3', size = 3},
            {name = 'test4', size = 4},
        },
    }
    cluster.server:exec(function(expected_threads_info)
        t.assert_equals(box.cfg.app_threads, 10)
        t.assert_equals(require('experimental.threads').info(),
                        expected_threads_info)
    end, {expected_threads_info})
    --
    -- Check configuration in other threads.
    --
    cluster.server:call('box.iproto.internal.enable_thread_requests')
    for i = 1, 10 do
        local group_name
        if i < 2 then
            group_name = 'test1'
        elseif i < 4 then
            group_name = 'test2'
        elseif i < 7 then
            group_name = 'test3'
        else
            group_name = 'test4'
        end
        expected_threads_info.thread_id = i
        expected_threads_info.group_name = group_name
        cluster.server:exec(function(expected_threads_info)
            t.assert_equals(require('experimental.threads').info(),
                            expected_threads_info)
        end, {expected_threads_info}, {_thread_id = i})
    end
end

g.test_threads_not_configured = function()
    --
    -- Check that if the threads configuration is unset, no thread groups
    -- are created, not even tx.
    --
    local config = cbuilder:new()
        :add_instance('server', {})
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_equals(box.cfg.app_threads, 0)
        local err = {type = 'ClientError', name = 'THREADS_NOT_CONFIGURED'}
        t.assert_error_covers(err, threads.info)
        t.assert_error_covers(err, threads.call)
        t.assert_error_covers(err, threads.eval)
    end)
end

g.test_thread_groups_not_configured = function()
    --
    -- Check that if the threads section is set in the config, the threads
    -- module is configured and the tx group is created even if no thread
    -- groups are configured.
    --
    local config = cbuilder:new()
        :add_instance('server', {})
        :set_instance_option('server', 'threads', {})
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_equals(threads.info(), {
            thread_id = 0,
            group_name = 'tx',
            groups = {{name = 'tx', size = 1}},
        })
    end)
end

g.test_thread_function_export = function()
    local config = cbuilder:new()
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 3},
            {name = 'test2', size = 2},
        })
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    --
    -- Argument checking.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'function name should be a string',
        }, threads.export, 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'function object should be a function',
        }, threads.export, 'test_func', 123)
    end)
    --
    -- Duplicate name.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        threads.export('test_func_1', function() end)
        threads.export('test_func_2', function() end)
        t.assert_error_covers({
            type = 'ClientError',
            name = 'FUNCTION_EXISTS',
        }, threads.export, 'test_func_1', function() end)
    end)
end

g.test_threads_call = function()
    local config = cbuilder:new()
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 3},
            {name = 'test2', size = 2},
        })
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    --
    -- Argument checking.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'group name should be a string',
        }, threads.call, 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'function name should be a string',
        }, threads.call, 'test1', 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'function arguments should be a table',
        }, threads.call, 'test1', 'test_func', 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'options should be a table',
        }, threads.call, 'test1', 'test_func', {123}, 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected option 'foo'",
        }, threads.call, 'test1', 'test_func', {123}, {foo = 'bar'})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options parameter 'target' should be of type string",
        }, threads.call, 'test1', 'test_func', {123}, {target = 123})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected value for option parameter 'target': " ..
                      "got 'foo', expected 'any' or 'all'",
        }, threads.call, 'test1', 'test_func', {123}, {target = 'foo'})
    end)
    --
    -- Unknown group.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_THREAD_GROUP',
        }, threads.call, 'test3', 'test_func', {}, {target = 'all'})
    end)
    --
    -- Unknown function.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_FUNCTION',
        }, threads.call, 'test1', 'test_func', {}, {target = 'all'})
    end)
    --
    -- Target handling.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local group = 'test1'
        local func = 'test_func_1'
        threads.eval(group, [[
            local threads = require('experimental.threads')
            threads.export('test_func_1', function()
                return threads.info().thread_id
            end)
        ]])
        local ret_all = threads.call(group, func, {}, {target = 'all'})
        t.assert_equals(ret_all, {{1}, {2}, {3}})
        local ret_any = threads.call(group, func, {}, {target = 'any'})
        t.assert_equals(#ret_any, 1)
        t.assert_items_include(ret_all, ret_any)
    end)
    --
    -- Default arguments.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local group = 'test2'
        local func = 'test_func_2'
        threads.eval(group, [[
            local threads = require('experimental.threads')
            threads.export('test_func_2', function(...) return ... end)
        ]])
        t.assert_equals(threads.call(group, func), {{}, {}})
        t.assert_equals(threads.call(group, func, {1, 2, 3}),
                        {{1, 2, 3}, {1, 2, 3}})
        t.assert_equals(threads.call(group, func, {1, 2, 3}, {}),
                        {{1, 2, 3}, {1, 2, 3}})
        t.assert_equals(threads.call(group, func, {1, 2, 3}, {target = 'all'}),
                        {{1, 2, 3}, {1, 2, 3}})
        t.assert_equals(threads.call(group, func, {1, 2, 3}, {target = 'any'}),
                        {{1, 2, 3}})
    end)
    --
    -- Return value and error handling.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local group = 'test1'
        threads.eval(group, [[
            local threads = require('experimental.threads')
            threads.export('test_func_3', function()
                local thread_id = threads.info().thread_id
                if thread_id == 1 then
                    return 1, 2, 3
                elseif thread_id == 3 then
                    return {1, 2, 3}
                end
            end)
        ]])
        t.assert_equals(
            threads.call(group, 'test_func_3', {}, {target = 'all'}),
            {{1, 2, 3}, {}, {{1, 2, 3}}}
        )
        threads.eval(group, [[
            local threads = require('experimental.threads')
            threads.export('test_func_4', function()
                local thread_id = threads.info().thread_id
                if thread_id == 1 then
                    box.error({type = 'MyErrorType', name = 'MyError1'})
                elseif thread_id == 2 then
                    box.error({type = 'MyErrorType', name = 'MyError2'})
                end
            end)
        ]])
        t.assert_error_covers(
            {type = 'MyErrorType', name = 'MyError2'},
            threads.call, group, 'test_func_4', {}, {target = 'all'}
        )
    end)
    --
    -- Calling tx thread group from tx.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local func = 'test_func_5'
        threads.eval('tx', [[
            local threads = require('experimental.threads')
            threads.export('test_func_5', threads.info)
        ]])
        local expected = {{threads.info()}}
        t.assert_equals(threads.call('tx', func, {}, {target = 'any'}),
                                     expected)
        t.assert_equals(threads.call('tx', func, {}, {target = 'all'}),
                                     expected)
    end)
    --
    -- Usage in other a non-tx thread.
    --
    cluster.server:call('box.iproto.internal.enable_thread_requests')
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_covers(threads.info(), {
            thread_id = 5,
            group_name = 'test2',
        })
        local func = 'test_func_6'
        local expr = [[
            local threads = require('experimental.threads')
            threads.export('test_func_6', function()
                local info = threads.info()
                return info.group_name, info.thread_id
            end)
        ]]
        threads.eval('tx', expr)
        threads.eval('test1', expr)
        threads.eval('test2', expr)
        t.assert_equals(threads.call('tx', func, {}, {target = 'all'}),
                        {{'tx', 0}})
        t.assert_equals(threads.call('test1', func, {}, {target = 'all'}),
                        {{'test1', 1}, {'test1', 2}, {'test1', 3}})
        t.assert_equals(threads.call('test2', func, {}, {target = 'all'}),
                        {{'test2', 4}, {'test2', 5}})
    end, {}, {_thread_id = 5})
end

g.test_threads_eval = function()
    local config = cbuilder:new()
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 3},
            {name = 'test2', size = 2},
        })
        :config()
    local cluster = cluster:new(config)
    cluster:start()
    --
    -- Argument checking.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'group name should be a string',
        }, threads.eval, 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'expression should be a string',
        }, threads.eval, 'test1', 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'expression arguments should be a table',
        }, threads.eval, 'test1', 'return ...', 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = 'options should be a table',
        }, threads.eval, 'test1', 'return ...', {123}, 123)
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected option 'foo'",
        }, threads.eval, 'test1', 'return ...', {123}, {foo = 'bar'})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "options parameter 'target' should be of type string",
        }, threads.eval, 'test1', 'return ...', {123}, {target = 123})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected value for option parameter 'target': " ..
                      "got 'foo', expected 'any' or 'all'",
        }, threads.eval, 'test1', 'return ...', {123}, {target = 'foo'})
    end)
    --
    -- Unknown group.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_error_covers({
            type = 'ClientError',
            name = 'NO_SUCH_THREAD_GROUP',
        }, threads.eval, 'test3', 'return ...', {}, {target = 'all'})
    end)
    --
    -- Target handling.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local group = 'test1'
        local expr = [[
            local threads = require('experimental.threads')
            return threads.info().thread_id
        ]]
        local ret_all = threads.eval(group, expr, {}, {target = 'all'})
        t.assert_equals(ret_all, {{1}, {2}, {3}})
        local ret_any = threads.eval(group, expr, {}, {target = 'any'})
        t.assert_equals(#ret_any, 1)
        t.assert_items_include(ret_all, ret_any)
    end)
    --
    -- Default arguments.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local group = 'test2'
        local expr = [[return ...]]
        t.assert_equals(threads.eval(group, expr), {{}, {}})
        t.assert_equals(threads.eval(group, expr, {1, 2, 3}),
                        {{1, 2, 3}, {1, 2, 3}})
        t.assert_equals(threads.eval(group, expr, {1, 2, 3}, {}),
                        {{1, 2, 3}, {1, 2, 3}})
        t.assert_equals(threads.eval(group, expr, {1, 2, 3}, {target = 'all'}),
                        {{1, 2, 3}, {1, 2, 3}})
        t.assert_equals(threads.eval(group, expr, {1, 2, 3}, {target = 'any'}),
                        {{1, 2, 3}})
    end)
    --
    -- Return value and error handling.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local group = 'test1'
        local expr = [[
            local threads = require('experimental.threads')
            local thread_id = threads.info().thread_id
            if thread_id == 1 then
                return 1, 2, 3
            elseif thread_id == 3 then
                return {1, 2, 3}
            end
        ]]
        t.assert_equals(threads.eval(group, expr, {}, {target = 'all'}),
                        {{1, 2, 3}, {}, {{1, 2, 3}}})
        expr = [[
            local threads = require('experimental.threads')
            local thread_id = threads.info().thread_id
            if thread_id == 1 then
                box.error({type = 'MyErrorType', name = 'MyError1'})
            elseif thread_id == 2 then
                box.error({type = 'MyErrorType', name = 'MyError2'})
            end
        ]]
        t.assert_error_covers({type = 'MyErrorType', name = 'MyError2'},
                              threads.eval, group, expr, {}, {target = 'all'})
    end)
    --
    -- Calling tx thread group from tx.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        local expr = [[
            return require('experimental.threads').info()
        ]]
        local expected = {{threads.info()}}
        t.assert_equals(threads.eval('tx', expr, {}, {target = 'any'}),
                                     expected)
        t.assert_equals(threads.eval('tx', expr, {}, {target = 'all'}),
                                     expected)
    end)
    --
    -- Usage in other a non-tx thread.
    --
    cluster.server:call('box.iproto.internal.enable_thread_requests')
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_covers(threads.info(), {
            thread_id = 5,
            group_name = 'test2',
        })
        local expr = [[
            local threads = require('experimental.threads')
            local info = threads.info()
            return info.group_name, info.thread_id
        ]]
        t.assert_equals(threads.eval('tx', expr, {}, {target = 'all'}),
                        {{'tx', 0}})
        t.assert_equals(threads.eval('test1', expr, {}, {target = 'all'}),
                        {{'test1', 1}, {'test1', 2}, {'test1', 3}})
        t.assert_equals(threads.eval('test2', expr, {}, {target = 'all'}),
                        {{'test2', 4}, {'test2', 5}})
    end, {}, {_thread_id = 5})
end
