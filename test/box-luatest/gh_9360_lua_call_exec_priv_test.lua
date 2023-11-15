local fun = require('fun')
local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

local function access_err(func, user)
    user = user or 'test'
    return string.format(
        "Execute access to function '%s' is denied for user '%s'", func, user)
end

local lua_funcs = {'lua_func_1', 'lua_func_2'}
local registered_lua_funcs = {'registered_lua_func_1', 'registered_lua_func_2'}
local stored_lua_funcs = {'stored_lua_func_1', 'stored_lua_func_2'}
local c_funcs = {'c_func_1', 'c_func_2'}
local builtins = {'box.session.user', 'box.session.uid'}
local registered_builtins = {'box.session.type', 'box.session.peer'}

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.server:exec(function(
            lua_funcs, registered_lua_funcs, stored_lua_funcs, c_funcs,
            registered_builtins)
        for _, f in ipairs(lua_funcs) do
            rawset(_G, f, function() return true end)
        end
        for _, f in ipairs(registered_lua_funcs) do
            rawset(_G, f, function() return true end)
            box.schema.func.create(f, {language = 'LUA'})
        end
        for _, f in ipairs(stored_lua_funcs) do
            box.schema.func.create(f, {
                language = 'LUA',
                body = [[function() return true end]],
            })
        end
        for _, f in ipairs(c_funcs) do
            box.schema.func.create(f, {language = 'C'})
        end
        for _, f in ipairs(registered_builtins) do
            box.schema.func.create(f, {language = 'LUA'})
        end
    end, {
        lua_funcs, registered_lua_funcs, stored_lua_funcs, c_funcs,
        registered_builtins
    })
    cg.grant = function(user_or_role, ...)
        cg.server:exec(function(user_or_role, ...)
            if box.schema.user.exists(user_or_role) then
                box.session.su('admin', box.schema.user.grant,
                               user_or_role, ...)
            else
                box.session.su('admin', box.schema.role.grant,
                               user_or_role, ...)
            end
        end, {user_or_role, ...})
    end
    cg.revoke = function(user_or_role, ...)
        cg.server:exec(function(user_or_role, ...)
            if box.schema.user.exists(user_or_role) then
                box.session.su('admin', box.schema.user.revoke,
                               user_or_role, ...)
            else
                box.session.su('admin', box.schema.role.revoke,
                               user_or_role, ...)
            end
        end, {user_or_role, ...})
    end
end)

g.after_all(function(cg)
    cg.server:drop()
    cg.server = nil
    cg.grant = nil
    cg.revoke = nil
end)

g.before_each(function(cg)
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.create,
                       'test', {password = 'secret'})
    end)
    cg.conn = net.connect(cg.server.net_box_uri, {
        user = 'test', password = 'secret',
    })
    t.assert_equals(cg.conn.state, 'active')
    cg.call = function(f)
        return cg.conn:call(f)
    end
end)

g.after_each(function(cg)
    cg.conn:close()
    cg.conn = nil
    cg.call = nil
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.drop, 'test')
    end)
end)

g.test_errors = function(cg)
    for _, priv in ipairs({
            'read', 'write', 'session', 'create', 'drop', 'alter',
            'reference', 'trigger', 'insert', 'update', 'delete'}) do
        local err = string.format("Unsupported lua_call privilege '%s'",
                                  priv)
        t.assert_error_msg_equals(err, cg.grant, 'test', priv, 'lua_call')
        t.assert_error_msg_equals(err, cg.grant, 'test', priv, 'lua_call',
                                  'dostring')
    end
    cg.grant('test', 'execute,usage', 'lua_call')
    cg.grant('test', 'execute,usage', 'lua_call', 'dostring')
    t.assert_error_msg_equals(
        "User 'test' already has execute access on lua_call ''",
        cg. grant, 'test', 'execute', 'lua_call')
    t.assert_error_msg_equals(
        "User 'test' already has usage access on lua_call ''",
        cg.grant, 'test', 'usage', 'lua_call')
    t.assert_error_msg_equals(
        "User 'test' already has execute access on lua_call 'dostring'",
        cg.grant, 'test', 'execute', 'lua_call', 'dostring')
    t.assert_error_msg_equals(
        "User 'test' already has usage access on lua_call 'dostring'",
        cg.grant, 'test', 'usage', 'lua_call', 'dostring')
end

g.test_no_access = function(cg)
    for _, f in fun.chain(
            lua_funcs, registered_lua_funcs, stored_lua_funcs, c_funcs,
            builtins, registered_builtins) do
        t.assert_error_msg_equals(access_err(f), cg.call, f)
    end
end

g.test_entity_execute_access = function(cg)
    cg.grant('test', 'execute', 'lua_call')
    for _, f in fun.chain(lua_funcs, registered_lua_funcs) do
        t.assert(cg.call(f))
    end
    for _, f in ipairs(
            stored_lua_funcs, c_funcs, builtins, registered_builtins) do
        t.assert_error_msg_equals(access_err(f), cg.call, f)
    end
end

g.test_object_execute_access = function(cg)
    local access = {
        [lua_funcs[1]] = true,
        [registered_lua_funcs[1]] = true,
        [stored_lua_funcs[1]] = true,
        [c_funcs[1]] = true,
        [builtins[1]] = true,
        [registered_builtins[1]] = true,
    }
    for f in pairs(access) do
        cg.grant('test', 'execute', 'lua_call', f)
    end
    for _, f in fun.chain(
            lua_funcs, registered_lua_funcs, builtins, registered_builtins) do
        if access[f] then
            t.assert(cg.call(f))
        else
            t.assert_error_msg_equals(access_err(f), cg.call, f)
        end
    end
    for _, f in fun.chain(stored_lua_funcs, c_funcs) do
        t.assert_error_msg_equals(access_err(f), cg.call, f)
    end
end

g.test_entity_plus_object_execute_access = function(cg)
    local access = {
        [lua_funcs[1]] = true,
        [registered_lua_funcs[1]] = true,
        [stored_lua_funcs[1]] = true,
        [c_funcs[1]] = true,
        [builtins[1]] = true,
        [registered_builtins[1]] = true,
    }
    cg.grant('test', 'execute', 'lua_call')
    for f in pairs(access) do
        cg.grant('test', 'execute', 'lua_call', f)
    end
    for _, f in fun.chain(lua_funcs, registered_lua_funcs) do
        t.assert(cg.call(f))
    end
    for _, f in ipairs(stored_lua_funcs, c_funcs) do
        t.assert_error_msg_equals(access_err(f), cg.call, f)
    end
    for _, f in fun.chain(builtins, registered_builtins) do
        if access[f] then
            t.assert(cg.call(f))
        else
            t.assert_error_msg_equals(access_err(f), cg.call, f)
        end
    end
end

g.test_undefined_func = function(cg)
    local err = "Procedure '%s' is not defined"
    cg.grant('test', 'execute', 'lua_call', 'no_such_func_1')
    t.assert_error_msg_equals(err:format('no_such_func_1'),
                              cg.call, 'no_such_func_1')
    t.assert_error_msg_equals(access_err('no_such_func_2'),
                              cg.call, 'no_such_func_2')
    cg.grant('test', 'execute', 'lua_call')
    t.assert_error_msg_equals(err:format('no_such_func_1'),
                              cg.call, 'no_such_func_1')
    t.assert_error_msg_equals(err:format('no_such_func_2'),
                              cg.call, 'no_such_func_2')
end

g.test_universal_access = function(cg)
    local function check()
        for _, f in fun.chain(
                lua_funcs, registered_lua_funcs, stored_lua_funcs,
                builtins, registered_builtins) do
            t.assert(cg.call(f))
        end
        for _, f in ipairs(c_funcs) do
            t.assert_error_msg_equals(
                string.format("Failed to dynamically load module '%s': " ..
                              "module not found", f),
                cg.call, f)
        end
    end
    cg.grant('test', 'execute', 'universe')
    check()
    cg.grant('test', 'execute', 'lua_call')
    check()
    for _, f in fun.chain(
            lua_funcs, registered_lua_funcs, stored_lua_funcs, c_funcs,
            builtins, registered_builtins) do
        cg.grant('test', 'execute', 'lua_call', f)
    end
    check()
end

g.test_usage_access = function(cg)
    local f = lua_funcs[1]
    cg.grant('test', 'execute', 'lua_call', f)
    t.assert(cg.call(f))
    cg.revoke('test', 'usage', 'universe')
    t.assert_error_msg_equals(access_err(f), cg.call, f)
    cg.grant('test', 'usage', 'lua_call', f)
    t.assert_error_msg_equals(access_err(f), cg.call, f)
    cg.revoke('test', 'usage', 'lua_call', f)
    cg.grant('test', 'usage', 'lua_call')
    t.assert(cg.call(f))
    cg.revoke('test', 'execute', 'lua_call', f)
    cg.grant('test', 'execute', 'lua_call')
    t.assert(cg.call(f))
    cg.revoke('test', 'usage', 'lua_call')
    t.assert_error_msg_equals(access_err(f), cg.call, f)
    cg.grant('test', 'usage', 'universe')
    t.assert(cg.call(f))
end

g.before_test('test_grant_revoke', function(cg)
    cg.server:exec(function()
        box.session.su('admin', function()
            box.schema.user.create('user1', {password = 'secret'})
            box.schema.user.create('user2', {password = 'secret'})
        end)
    end)
    cg.conn1 = net.connect(cg.server.net_box_uri, {
        user = 'user1', password = 'secret',
    })
    t.assert_equals(cg.conn1.state, 'active')
    cg.call1 = function(f)
        return cg.conn1:call(f)
    end
    cg.conn2 = net.connect(cg.server.net_box_uri, {
        user = 'user2', password = 'secret',
    })
    t.assert_equals(cg.conn2.state, 'active')
    cg.call2 = function(f)
        return cg.conn2:call(f)
    end
end)

g.test_grant_revoke = function(cg)
    local u1 = 'user1'
    local u2 = 'user2'
    local f1 = 'lua_func_1'
    local f2 = 'lua_func_2'
    cg.grant(u1, 'execute', 'lua_call', f1)
    cg.grant(u2, 'execute', 'lua_call', f2)
    t.assert(cg.call1(f1))
    t.assert(cg.call2(f2))
    t.assert_error_msg_equals(access_err(f1, u2), cg.call2, f1)
    t.assert_error_msg_equals(access_err(f2, u1), cg.call1, f2)
    cg.grant(u1, 'execute', 'lua_call', f2)
    cg.grant(u2, 'execute', 'lua_call', f1)
    t.assert(cg.call1(f1))
    t.assert(cg.call1(f2))
    t.assert(cg.call2(f1))
    t.assert(cg.call2(f2))
    cg.revoke(u1, 'execute', 'lua_call', f1)
    cg.revoke(u2, 'execute', 'lua_call', f2)
    t.assert(cg.call1(f2))
    t.assert(cg.call2(f1))
    t.assert_error_msg_equals(access_err(f2, u2), cg.call2, f2)
    t.assert_error_msg_equals(access_err(f1, u1), cg.call1, f1)
    cg.revoke(u1, 'execute', 'lua_call', f2)
    cg.revoke(u2, 'execute', 'lua_call', f1)
    t.assert_error_msg_equals(access_err(f1, u1), cg.call1, f1)
    t.assert_error_msg_equals(access_err(f2, u1), cg.call1, f2)
    t.assert_error_msg_equals(access_err(f1, u2), cg.call2, f1)
    t.assert_error_msg_equals(access_err(f2, u2), cg.call2, f2)
end

g.after_test('test_grant_revoke', function(cg)
    cg.conn1 = nil
    cg.conn2 = nil
    cg.call1 = nil
    cg.call2 = nil
    cg.server:exec(function()
        box.session.su('admin', function()
            box.schema.user.drop('user1', {if_exists = true})
            box.schema.user.drop('user2', {if_exists = true})
        end)
    end)
end)

g.before_test('test_role', function(cg)
    cg.server:exec(function()
        box.session.su('admin', function()
            box.schema.role.create('role1')
            box.schema.role.create('role2')
        end)
    end)
end)

g.test_role = function(cg)
    local f1 = 'lua_func_1'
    local f2 = 'lua_func_2'
    cg.grant('role1', 'execute', 'lua_call', f1)
    cg.grant('role2', 'execute', 'lua_call', f2)
    cg.grant('test', 'execute', 'role', 'role1')
    cg.grant('test', 'execute', 'role', 'role2')
    t.assert(cg.call(f1))
    t.assert(cg.call(f2))
    cg.grant('role1', 'execute', 'lua_call', f2)
    cg.grant('role2', 'execute', 'lua_call', f1)
    t.assert(cg.call(f1))
    t.assert(cg.call(f2))
    cg.revoke('role1', 'execute', 'lua_call', f1)
    cg.revoke('role2', 'execute', 'lua_call', f2)
    t.assert(cg.call(f1))
    t.assert(cg.call(f2))
    cg.revoke('role1', 'execute', 'lua_call', f2)
    cg.revoke('role2', 'execute', 'lua_call', f1)
    t.assert_error_msg_equals(access_err(f1), cg.call, f1)
    t.assert_error_msg_equals(access_err(f2), cg.call, f2)
    cg.grant('role1', 'execute', 'lua_call', f1)
    cg.grant('role2', 'execute', 'lua_call', f2)
    t.assert(cg.call(f1))
    t.assert(cg.call(f2))
    cg.revoke('test', 'execute', 'role', 'role1')
    cg.revoke('test', 'execute', 'role', 'role2')
    t.assert_error_msg_equals(access_err(f1), cg.call, f1)
    t.assert_error_msg_equals(access_err(f2), cg.call, f2)
end

g.after_test('test_role', function(cg)
    cg.server:exec(function()
        box.session.su('admin', function()
            box.schema.role.drop('role1', {if_exists = true})
            box.schema.role.drop('role2', {if_exists = true})
        end)
    end)
end)
