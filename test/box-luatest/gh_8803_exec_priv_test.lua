local net = require('net.box')
local server = require('luatest.server')
local t = require('luatest')

local g_common = t.group('gh_8803_exec_priv.common', t.helpers.matrix({
    obj_type = {'lua_call', 'lua_eval', 'sql'},
}))

g_common.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g_common.after_all(function(cg)
    cg.server:drop()
end)

g_common.before_each(function(cg)
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.create, 'test')
    end)
end)

g_common.after_each(function(cg)
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.drop, 'test')
    end)
end)

-- Checks the error raised on grant of unsupported privileges.
g_common.test_unsupported_privs = function(cg)
    cg.server:exec(function(obj_type)
        local unsupported_privs = {
            'read', 'write', 'session', 'create', 'drop', 'alter',
            'reference', 'trigger', 'insert', 'udpate', 'delete',
        }
        for _, priv in ipairs(unsupported_privs) do
            t.assert_error_msg_equals(
                string.format("Unsupported %s privilege '%s'", obj_type, priv),
                box.schema.user.grant, 'test', priv, obj_type)
        end
    end, {cg.params.obj_type})
end

-- Checks that global execute access may be granted only by admin.
g_common.test_grant = function(cg)
    cg.server:exec(function(obj_type)
        box.session.su('admin', box.schema.user.grant, 'test', 'super')
        t.assert_error_msg_equals(
            string.format("Grant access to %s '' is denied for user 'test'",
                          obj_type),
            box.session.su, 'test', box.schema.user.grant, 'test', 'execute',
            obj_type)
    end, {cg.params.obj_type})
end

local g = t.group('gh_8803_exec_priv')

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
    cg.grant = function(...)
        cg.server:exec(function(...)
            box.session.su('admin', box.schema.user.grant, ...)
        end, {...})
    end
    cg.revoke = function(...)
        cg.server:exec(function(...)
            box.session.su('admin', box.schema.user.revoke, ...)
        end, {...})
    end
    cg.server:exec(function()
        rawset(_G, 'lua_func', function() return true end)
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.before_each(function(cg)
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.create, 'test',
                       {password = 'secret'})
    end)
    cg.conn = net.connect(cg.server.net_box_uri, {
        user = 'test', password = 'secret',
    })
end)

g.after_each(function(cg)
    cg.conn:close()
    cg.server:exec(function()
        box.session.su('admin', box.schema.user.drop, 'test')
    end)
end)

-- Checks that execute privilege granted on lua_eval grants access to
-- evaluating an arbitrary Lua expression.
g.test_lua_eval = function(cg)
    local c = cg.conn
    local expr = 'return true'
    local errmsg = "Execute access to universe '' is denied for user 'test'"
    t.assert_error_msg_equals(errmsg, c.eval, c, expr)
    cg.grant('test', 'execute', 'lua_eval')
    t.assert(pcall(c.eval, c, expr))
    cg.revoke('test', 'usage', 'universe')
    t.assert_error_msg_equals(errmsg, c.eval, c, expr)
    cg.grant('test', 'usage', 'lua_eval')
    t.assert(pcall(c.eval, c, expr))
end

-- Checks that execute privilege granted on lua_call grants access to
-- any global user-defined Lua function.
g.test_lua_call = function(cg)
    local c = cg.conn
    local errmsg = "Execute access to function 'lua_func' is denied " ..
                   "for user 'test'"
    t.assert_error_msg_equals(errmsg, c.call, c, 'lua_func')
    cg.grant('test', 'execute', 'lua_call')
    t.assert(pcall(c.call, c, 'lua_func'))
    cg.revoke('test', 'usage', 'universe')
    t.assert_error_msg_equals(errmsg, c.call, c, 'lua_func')
    cg.grant('test', 'usage', 'lua_call')
    t.assert(pcall(c.call, c, 'lua_func'))
end

g.before_test('test_lua_call_func', function(cg)
    cg.server:exec(function()
        box.schema.func.create('c_func', {language = 'C'})
        box.schema.func.create('lua_func', {language = 'LUA'})
        box.schema.func.create('stored_lua_func', {
            language = 'LUA',
            body = [[function() return true end]],
        })
    end)
end)

g.after_test('test_lua_call_func', function(cg)
    cg.server:exec(function()
        box.schema.func.drop('c_func')
        box.schema.func.drop('lua_func')
        box.schema.func.drop('stored_lua_func')
    end)
end)

-- Checks that execute privilege granted on lua_call does not grant access to
-- Lua functions from _func.
g.test_lua_call_func = function(cg)
    local c = cg.conn
    local errfmt = "Execute access to function '%s' is denied for user 'test'"
    local func_list = {'c_func', 'lua_func', 'stored_lua_func'}
    cg.grant('test', 'execute', 'lua_call')
    for _, func in ipairs(func_list) do
        t.assert_error_msg_equals(errfmt:format(func), c.call, c, func)
    end
end

-- Checks that execute privilege granted on lua_call does not grant access
-- to built-in Lua functions.
g.test_lua_call_builtin = function(cg)
    local c = cg.conn
    local errfmt = "Execute access to function '%s' is denied for user 'test'"
    local func_list = {
        'load', 'loadstring', 'loadfile', 'rawset', 'pcall',
        'box.cfg', 'box["cfg"]', 'box.session.su', 'box:error',
    }
    cg.grant('test', 'execute', 'lua_call')
    for _, func in ipairs(func_list) do
        t.assert_error_msg_equals(errfmt:format(func), c.call, c, func)
    end
end

-- Checks that execute privilege granted on sql grants access to
-- executing SQL expressions.
g.test_sql = function(cg)
    local c = cg.conn
    local expr = 'SELECT 1'
    local errmsg = "Execute access to sql '' is denied for user 'test'"
    t.assert_error_msg_equals(errmsg, c.execute, c, expr)
    t.assert_error_msg_equals(errmsg, c.prepare, c, expr)
    t.assert_error_msg_equals(errmsg, c.unprepare, c, 0)
    cg.grant('test', 'execute', 'sql')
    t.assert(pcall(c.execute, c, expr))
    cg.revoke('test', 'usage', 'universe')
    t.assert_error_msg_equals(errmsg, c.execute, c, expr)
    t.assert_error_msg_equals(errmsg, c.prepare, c, expr)
    t.assert_error_msg_equals(errmsg, c.unprepare, c, 0)
    cg.grant('test', 'usage', 'sql')
    t.assert(pcall(c.execute, c, expr))
end

g.after_test('test_sql_compat', function(cg)
    cg.server:exec(function()
        local compat = require('compat')
        compat.sql_priv = 'default'
    end)
end)

-- Checks the sql_priv compat option.
g.test_sql_compat = function(cg)
    local c = cg.conn
    local expr = 'SELECT 1'
    local errmsg = "Execute access to sql '' is denied for user 'test'"
    t.assert_error_msg_equals(errmsg, c.execute, c, expr)
    cg.server:exec(function()
        local compat = require('compat')
        t.assert_equals(compat.sql_priv.default, 'new')
        t.assert_equals(compat.sql_priv.current, 'default')
        compat.sql_priv = 'old'
    end)
    t.assert(pcall(c.execute, c, expr))
    cg.server:exec(function()
        local compat = require('compat')
        compat.sql_priv = 'new'
    end)
    t.assert_error_msg_equals(errmsg, c.execute, c, expr)
end
