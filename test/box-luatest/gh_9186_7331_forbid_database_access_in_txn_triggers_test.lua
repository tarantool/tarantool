local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_database_access_in_txn_triggers = function(cg)
    cg.server:exec(function()
        local trigger = require('trigger')

        -- Create spaces of all types (ordinary, temporary, data-temporary)
        local s = box.schema.create_space('s')
        s:create_index('pk')
        local ts = box.schema.create_space('ts', {type = 'temporary'})
        ts:create_index('pk')
        local dts = box.schema.create_space('dts', {type = 'data-temporary'})
        dts:create_index('pk')

        -- Create user to check DCL
        box.schema.user.create('test_user')

        -- A global savepoint used to test rollback_to_savepoint, set later
        local svp = nil

        -- Checker for old triggers (box.on_commit and box.on_rollback)
        local function check_case_old(trigger, action, finalizer, expected_err)
            box.begin()
            s:replace{0, 0}
            svp = box.savepoint()
            local err = nil
            local function trigger_f()
                local _, errmsg = pcall(action)
                err = errmsg
            end
            trigger(trigger_f)
            finalizer()
            t.assert_equals(err.message, expected_err)
        end

        -- Checker for new triggers (using module trigger)
        local function check_case_new(event, action, finalizer, expected_err)
            local err = nil
            local function trigger_f()
                local _, errmsg = pcall(action)
                err = errmsg
            end
            trigger.set(event, "test_trigger", trigger_f)

            box.begin()
            svp = box.savepoint()
            s:replace{0, 0}
            finalizer()

            trigger.del(event, "test_trigger")
            t.assert_equals(err.message, expected_err)
        end

        -- Cases that should fail in transactional triggers
        local cases = {
            -- DML
            function() s:replace{1, 1} end,
            function() ts:replace{1, 1} end,
            function() dts:replace{1, 1} end,

            -- DQL
            function() s:select{} end,
            function() s:get{0} end,
            function() ts:select{} end,
            function() ts:get{0} end,
            function() dts:select{} end,
            function() dts:get{0} end,

            -- DDL
            function() box.schema.create_space('new_space') end,
            function() s:create_index('sk') end,
            function() box.schema.func.create('new_func') end,
            function() box.schema.user.create('new_user') end,

            -- DCL
            function()
                box.schema.user.grant('test_user', 'read,write', 'space', 's')
            end,
            function()
                box.schema.user.revoke('test_user', 'read,write', 'space', 's')
            end,

            -- TCL
            function() box.commit() end,
            function() box.rollback() end,
            function() box.rollback_to_savepoint(svp) end,
            function() box.savepoint() end,
        }

        for _, case in pairs(cases) do
            check_case_old(box.on_commit, case, box.commit, 'Transaction was committed')
            check_case_old(box.on_rollback, case, box.rollback, 'Transaction was rolled back')

            check_case_new('box.on_commit', case, box.commit, 'Transaction was committed')
            check_case_new('box.on_rollback', case, box.rollback, 'Transaction was rolled back')
        end
    end)
end
