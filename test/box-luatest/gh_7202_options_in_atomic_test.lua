local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all = function()
    g.server = server:new{
        alias  = 'default',
        box_cfg = {
            txn_isolation = 'best-effort',
        }
    }
    g.server:start()
end

g.after_all = function()
    g.server:drop()
end

-- Test of box.atomic with or without options.
g.test_atomic_options = function()
    g.server:exec(function()
        local lt = require('luatest')

        -- Simple function
        local function f(...)
            return box.internal.txn_isolation(), ...
        end
        -- Simple callable table
        local t = setmetatable({'table'}, {__call = f})

        -- Atomic without options
        lt.assert_equals({box.atomic(f, 1, 2)},
            {box.txn_isolation_level.BEST_EFFORT, 1, 2})
        lt.assert_equals({box.atomic(t, 1, 2)},
            {box.txn_isolation_level.BEST_EFFORT, {'table'}, 1, 2})

        -- Atomic with empty options
        lt.assert_equals({box.atomic({}, f, 1, 2)},
            {box.txn_isolation_level.BEST_EFFORT, 1, 2})
        lt.assert_equals({box.atomic({}, t, 1, 2)},
            {box.txn_isolation_level.BEST_EFFORT, {'table'}, 1, 2})

        -- Atomic with options
        local opts = {txn_isolation = 'read-committed'}
        lt.assert_equals({box.atomic(opts, f, 1, 2)},
            {box.txn_isolation_level.READ_COMMITTED, 1, 2})
        lt.assert_equals({box.atomic(opts, t, 1, 2)},
            {box.txn_isolation_level.READ_COMMITTED, {'table'}, 1, 2})

        -- Different invalid options
        lt.assert_error_msg_equals("Illegal parameters, unexpected option 'aa'",
            box.atomic, {aa = 'bb'}, f)

        lt.assert_error_msg_equals("Illegal parameters, unexpected option 'aa'",
            box.atomic, {txn_isolation = 0, aa = 'bb'}, f)

        lt.assert_error_msg_equals(
            "Illegal parameters, " ..
            "txn_isolation must be one of box.txn_isolation_level " ..
            "(keys or values)",
            box.atomic, {txn_isolation = 'wtf'}, f)

        lt.assert_error_msg_contains(
            "attempt to call a nil value",
            box.atomic, {})

        -- Different invalid function objects
        lt.assert_error_msg_contains(
            "attempt to call a table value",
            box.atomic, {}, {})

        lt.assert_error_msg_contains(
            "attempt to call a nil value",
            box.atomic, {})
    end)
end
