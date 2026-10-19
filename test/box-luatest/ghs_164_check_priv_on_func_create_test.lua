local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.before_each(function(g)
    g.server:exec(function()
        -- Create a user able to do the DDLs required for the tests.
        box.schema.user.create('u')
        box.schema.user.grant('u', 'create', 'space')
        box.schema.user.grant('u', 'create', 'function')
        box.schema.user.grant('u', 'read,write', 'space', '_func')
    end)
end)

g.after_each(function(g)
    g.server:exec(function()
        box.schema.user.drop('u', {if_exists = true})
        box.schema.func.drop('test', {if_exists = true})
    end)
end)

-- A sandboxed function runs in a restricted environment, so it needs no
-- extra privileges.
g.test_sandboxed_func_create = function(g)
    g.server:exec(function()
        box.session.su('u', box.schema.func.create, 'test', {
            body = 'function() end',
            is_sandboxed = true,
        })
        t.assert_not_equals(box.func.test, nil)
    end)
end

-- Creating a non-sandboxed function needs execute on universe: it runs
-- arbitrary code.
g.test_non_sandboxed_func_create_denied = function(g)
    g.server:exec(function()
        local err = {
            type = 'AccessDeniedError',
            message = "Execute access to universe '' is denied for user 'u'",
            details = "Failed to create function 'test': Can't create" ..
                      " `is_sandboxed = false` function without" ..
                      " execute privilege on universe.",
        }
        local cases = {
            -- A Lua function with a body.
            {body = 'function() end'},
            -- A Lua function with an explicit non-sandboxed flag.
            {body = 'function() end', is_sandboxed = false},
            -- A function registering no triggers (empty trigger list).
            {body = 'function() end', trigger = {}},
            -- A persistent C function.
            {language = 'C'},
            -- A function from the global namespace.
            {},
            -- A function registering a trigger, whichever the event is.
            {body = 'function() end', trigger = 'box.session.on_connect'},
            {body = 'function() end', trigger = 'box.ctl.on_schema_init'},
        }
        for _, opts in ipairs(cases) do
            t.assert_error_covers(
                err, box.session.su, 'u', box.schema.func.create, 'test', opts)
            t.assert_equals(box.func.test, nil)
        end
    end)
end

-- The check is enforced on the _func space itself, not only in the Lua API.
g.test_func_direct_insert_denied = function(g)
    g.server:exec(function()
        box.schema.func.create('test', {body = 'function() end'})
        -- Reuse a valid tuple of a non-sandboxed function.
        local tuple = box.space._func.index.name:get('test'):totable()
        box.func.test:drop()
        t.assert_equals(box.func.test, nil)
        local err = {
            type = 'AccessDeniedError',
            message = "Execute access to universe '' is denied for user 'u'",
            details = "Failed to create function 'test': Can't create" ..
                      " `is_sandboxed = false` function without" ..
                      " execute privilege on universe.",
        }
        t.assert_error_covers(
            err, box.session.su, 'u', box.space._func.insert,
            box.space._func, tuple)
        t.assert_equals(box.func.test, nil)
    end)
end

-- A user with execute on universe can create non-sandboxed functions.
g.test_func_create_granted = function(g)
    g.server:exec(function()
        -- Create a non-sandboxed function.
        box.schema.user.grant('u', 'execute', 'universe')
        box.session.su('u', box.schema.func.create, 'test', {
            body = 'function() end',
            is_sandboxed = false,
        })
        t.assert_not_equals(box.func.test, nil)
        box.func.test:drop()

        -- Revoke the privilege and try again (expect a failure).
        local err = {
            type = 'AccessDeniedError',
            message = "Execute access to universe '' is denied for user 'u'",
            details = "Failed to create function 'test': Can't create" ..
                      " `is_sandboxed = false` function without" ..
                      " execute privilege on universe.",
        }
        box.schema.user.revoke('u', 'execute', 'universe')
        t.assert_error_covers(
            err, box.session.su, 'u', box.schema.func.create, 'test', {
                body = 'function() end',
                is_sandboxed = false,
            })
        t.assert_equals(box.func.test, nil)
    end)
end

-- Admin can always create non-sandboxed functions.
g.test_func_create_admin = function(g)
    g.server:exec(function()
        box.schema.func.create('test', {body = 'function() end'})
        t.assert_not_equals(box.func.test, nil)
        box.func.test:drop()
    end)
end
