local net = require('net.box')

local t = require('luatest')
local cbuilder = require('luatest.cbuilder')
local cluster = require('luatest.cluster')
local server = require('luatest.server')

local g = t.group()

local BASE_CONFIG = cbuilder:new()
    :set_global_option('credentials.users.admin.password', 'secret')
    :config()

local SERVER_OPTS = {
    net_box_credentials = {user = 'admin', password = 'secret'},
}

--
-- FIXME(gh-12546): For server.exec to be able to find the luatest module,
-- searchroot must be set in the test server. For the main thread, this is
-- done automatically by luatest itself, but due to the bug in Tarantool,
-- searchroot isn't propagated to application threads so we have to set it
-- manually. Remove this when the bug is fixed.
--
local function setsearchroot(server)
    local searchroot, app_threads = server:exec(function()
        return package.searchroot(), box.cfg.app_threads
    end)
    for i = 1, app_threads do
        server:eval('package.setsearchroot(...)', {searchroot},
                    {_thread_id = i})
    end
end

g.test_threads_config_propagation = function()
    --
    -- Check that the threads configuration is propagated to box.cfg
    -- and the threads module.
    --
    local config = cbuilder:new(BASE_CONFIG)
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 1},
            {name = 'test2', size = 2},
            {name = 'test3', size = 3},
        })
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
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
    config = cbuilder:new(BASE_CONFIG)
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
    setsearchroot(cluster.server)
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
    local config = cbuilder:new(BASE_CONFIG)
        :add_instance('server', {})
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
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
    local config = cbuilder:new(BASE_CONFIG)
        :add_instance('server', {})
        :set_instance_option('server', 'threads', {})
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
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
    local config = cbuilder:new(BASE_CONFIG)
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 3},
            {name = 'test2', size = 2},
        })
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
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
    local config = cbuilder:new(BASE_CONFIG)
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 3},
            {name = 'test2', size = 2},
        })
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
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
            message = "options parameter 'target' should be one of types: " ..
                      "string, number",
        }, threads.call, 'test1', 'test_func', {123}, {target = {}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected value for option parameter 'target': " ..
                      "got 'foo', expected 'any', 'all', or a number",
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
    -- Unknown thread.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        for _, p in ipairs({{'test1', -1}, {'test2', 0},
                            {'test1', 4}, {'test2', 3}}) do
            local group, target = unpack(p)
            t.assert_error_covers({
                type = 'ClientError',
                name = 'NO_SUCH_THREAD',
                thread_id = target,
            }, threads.call, group, 'test_func', {}, {target = target})
        end
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
        local func_def = [[
            local threads = require('experimental.threads')
            threads.export('test_func_1', function()
                return threads.info().thread_id
            end)
        ]]
        threads.eval('test1', func_def)
        local ret_all = threads.call(group, func, {}, {target = 'all'})
        t.assert_equals(ret_all, {{1}, {2}, {3}})
        local ret_any = threads.call(group, func, {}, {target = 'any'})
        t.assert_equals(#ret_any, 1)
        t.assert_items_include(ret_all, ret_any)
        t.assert_equals(threads.call('test1', func, {}, {target = 1}), {{1}})
        t.assert_equals(threads.call('test1', func, {}, {target = 2}), {{2}})
        t.assert_equals(threads.call('test1', func, {}, {target = 3}), {{3}})
        threads.eval('test2', func_def)
        t.assert_equals(threads.call('test2', func, {}, {target = 1}), {{4}})
        t.assert_equals(threads.call('test2', func, {}, {target = 2}), {{5}})
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
    setsearchroot(cluster.server)
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
    local config = cbuilder:new(BASE_CONFIG)
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 3},
            {name = 'test2', size = 2},
        })
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
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
            message = "options parameter 'target' should be one of types: " ..
                      "string, number",
        }, threads.eval, 'test1', 'return ...', {123}, {target = {}})
        t.assert_error_covers({
            type = 'IllegalParams',
            message = "unexpected value for option parameter 'target': " ..
                      "got 'foo', expected 'any', 'all', or a number",
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
    -- Unknown thread.
    --
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        for _, p in ipairs({{'test1', -1}, {'test2', 0},
                            {'test1', 4}, {'test2', 3}}) do
            local group, target = unpack(p)
            t.assert_error_covers({
                type = 'ClientError',
                name = 'NO_SUCH_THREAD',
                thread_id = target,
            }, threads.eval, group, 'return ...', {}, {target = target})
        end
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
        t.assert_equals(threads.eval('test1', expr, {}, {target = 1}), {{1}})
        t.assert_equals(threads.eval('test1', expr, {}, {target = 2}), {{2}})
        t.assert_equals(threads.eval('test1', expr, {}, {target = 3}), {{3}})
        t.assert_equals(threads.eval('test2', expr, {}, {target = 1}), {{4}})
        t.assert_equals(threads.eval('test2', expr, {}, {target = 2}), {{5}})
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
    setsearchroot(cluster.server)
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

g.test_threads_priv = function()
    local config = cbuilder:new(BASE_CONFIG)
        :set_global_option('credentials.users.guest.privileges', {
            {
                permissions = {'execute'},
                lua_call = {'test_func_1', 'test_func_2'},
            },
        })
        :set_global_option('credentials.users.admin.password', 'secret')
        :set_global_option('credentials.users.alice.password', 'ALICE')
        :set_global_option('credentials.users.alice.privileges', {
            {
                permissions = {'execute'},
                lua_call = {'test_func_1'},
            },
        })
        :set_global_option('credentials.users.bob.password', 'BOB')
        :set_global_option('credentials.users.bob.privileges', {
            {
                permissions = {'execute'},
                lua_call = {'all'},
            },
        })
        :add_instance('server', {})
        :set_instance_option('server', 'threads.groups', {
            {name = 'test1', size = 1},
            {name = 'test2', size = 2},
            {name = 'test3', size = 3},
        })
        :config()
    local cluster = cluster:new(config, SERVER_OPTS)
    cluster.server:start()
    cluster.server:exec(function()
        local threads = require('experimental.threads')
        for _, group in ipairs(threads.info().groups) do
            threads.eval(group.name, [[
                for _, f in ipairs({
                    'test_func_1', 'test_func_2', 'test_func_3',
                }) do
                    box.iproto.export(f, function() return f end)
                end
            ]])
        end
    end)
    local conn_guest = net.connect(cluster.server.net_box_uri)
    local conn_admin = net.connect(cluster.server.net_box_uri,
                                   {user = 'admin', password = 'secret'})
    local conn_alice = net.connect(cluster.server.net_box_uri,
                                   {user = 'alice', password = 'ALICE'})
    local conn_bob = net.connect(cluster.server.net_box_uri,
                                 {user = 'bob', password = 'BOB'})
    local function check_ok(conn, func_name)
        t.assert_equals(conn:call(func_name), func_name)
    end
    local function check_err(conn, func_name)
        t.assert_error_covers({
            type = 'AccessDeniedError',
            access_type = 'Execute',
            object_type = 'function',
            object_name = func_name,
            user = conn.opts and conn.opts.user or 'guest',
        }, conn.call, conn, func_name)
    end
    -- Run the checks a few times so that function call requests land in
    -- different application threads.
    for _ = 1, 10 do
        check_ok(conn_admin, 'test_func_1')
        check_ok(conn_admin, 'test_func_2')
        check_ok(conn_admin, 'test_func_3')
        check_ok(conn_guest, 'test_func_1')
        check_ok(conn_guest, 'test_func_2')
        check_err(conn_guest, 'test_func_3')
        check_ok(conn_alice, 'test_func_1')
        check_err(conn_alice, 'test_func_2')
        check_err(conn_alice, 'test_func_3')
        check_ok(conn_bob, 'test_func_1')
        check_ok(conn_bob, 'test_func_2')
        check_ok(conn_bob, 'test_func_3')
    end
    -- Reload config and check again.
    config = cbuilder:new(config)
        :set_global_option('credentials.users.guest.privileges', {
            {
                permissions = {'execute'},
                lua_call = {'test_func_3'},
            },
        })
        :set_global_option('credentials.users.alice.privileges', {
            {
                permissions = {'execute'},
                lua_call = {'all'},
            },
        })
        :set_global_option('credentials.users.bob.privileges', {
            {
                permissions = {'execute'},
                lua_call = {'test_func_2'},
            },
        })
        :config()
    cluster:reload(config)
    for _ = 1, 10 do
        check_ok(conn_admin, 'test_func_1')
        check_ok(conn_admin, 'test_func_2')
        check_ok(conn_admin, 'test_func_3')
        check_err(conn_guest, 'test_func_1')
        check_err(conn_guest, 'test_func_2')
        check_ok(conn_guest, 'test_func_3')
        check_ok(conn_alice, 'test_func_1')
        check_ok(conn_alice, 'test_func_2')
        check_ok(conn_alice, 'test_func_3')
        check_err(conn_bob, 'test_func_1')
        check_ok(conn_bob, 'test_func_2')
        check_err(conn_bob, 'test_func_3')
    end
end

g.before_test('test_box_cfg', function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.test_box_cfg = function(cg)
    --
    -- Check that the threads module is initialized with the default thread
    -- group if box.cfg.app_threads is set from application code.
    --
    cg.server:exec(function()
        local threads = require('experimental.threads')
        local err = {type = 'ClientError', name = 'THREADS_NOT_CONFIGURED'}
        t.assert_error_covers(err, threads.info)
        t.assert_error_covers(err, threads.call)
        t.assert_error_covers(err, threads.eval)
    end)
    cg.server:restart({box_cfg = {app_threads = 4}})
    cg.server:exec(function()
        local threads = require('experimental.threads')
        t.assert_equals(threads.info(), {
            thread_id = 0,
            group_name = 'tx',
            groups = {
                {name = 'tx', size = 1},
                {name = 'app', size = 4},
            },
        })
        threads.eval('app', [[
            local threads = require('experimental.threads')
            threads.export('test_func', function()
                local info = threads.info()
                return info.group_name, info.thread_id
            end)
        ]])
        t.assert_equals(threads.call('app', 'test_func'), {
            {'app', 1}, {'app', 2}, {'app', 3}, {'app', 4},
        })
    end)
end

g.after_test('test_box_cfg', function(cg)
    cg.server:drop()
    cg.server = nil
end)
