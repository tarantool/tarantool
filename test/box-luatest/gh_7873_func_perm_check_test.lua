local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

local function test_func_indexes()
    local value
    local prefix = 'test_func_indexes_'
    local uname = prefix .. 'user'
    local fname = prefix .. 'func'
    local s_test = box.schema.space.create(prefix .. 's_test')
    local err = "Execute access to function '" .. fname .. "'"
                .. " is denied for user '" .. uname .. "'"

    -- Fill spaces to invoke format/constraint check.
    s_test:create_index('pk')
    for i = 1, 10 do s_test:insert({i}) end

    -- Create a function by 'admin'.
    box.schema.func.create(fname, {
        body = 'function(tuple) return { tuple[1] } end',
        is_deterministic = true,
        is_sandboxed = true
    })

    -- Set the function as functional index function.
    value = {parts = {{1, 'number'}}, unique = true, func = fname}
    s_test:create_index('fk1', value)

    -- Create a restricted user.
    box.session.su('admin')
    box.schema.user.create(uname)
    box.schema.user.grant(uname, 'read,write,alter,create,drop', 'universe')

    -- Switch to the restricted user.
    box.session.su(uname)

    ----------------------------------------------------------------------------
    -- The restricted user should be able to use the space with a functional
    -- index using the function that was created by 'admin'.

    s_test:insert({42})

    ----------------------------------------------------------------------------
    -- The restricted user should not be able to specify the function that was
    -- created by 'admin' as a functional index function.

    value = {parts = {{1, 'unsigned'}}, unique = true, func = fname}
    t.assert_error_msg_equals(err, s_test.create_index, s_test, 'fk2', value)

    ----------------------------------------------------------------------------
    -- Cleanup.

    box.session.su('admin')
    s_test:drop()
    box.schema.user.drop(uname)
    box.schema.func.drop(fname)
end

g.test_func_indexes = function(cg)
    cg.server:exec(test_func_indexes)
end
