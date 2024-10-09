local server = require('luatest.server')
local t = require('luatest')

local g = t.group("Upgrade from 2.11.0")

g.before_all(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/2.11.0',
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.schema.downgrade('2.11.0')
        box.snapshot()
    end)
end)

g.test_new_replicaset_uuid_key = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
        box.schema.upgrade()
        local _schema = box.space._schema
        t.assert_equals(_schema:get{'cluster'}, nil)
        t.assert_equals(_schema:get{'replicaset_uuid'}.value,
                        box.info.replicaset.uuid)
    end)
end

g.test_ddl_is_forbidden = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
        -- DDL is prohibited before 2.11.1.
        local msg = "DDL_BEFORE_UPGRADE requires schema version 2.11.1"
        t.assert_error_msg_contains(msg, function()
            box.schema.space.create('test')
        end)

        box.schema.upgrade()
    end)
end

local g_2_11_1 = t.group("Upgrade from 2.11.1")

g_2_11_1.before_all(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/2.11.1',
    })
    cg.server:start()
end)

g_2_11_1.after_all(function(cg)
    cg.server:drop()
end)

g_2_11_1.test_allowed_before_upgrade = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 1})
        -- DDL is allowed before upgrade.
        box.schema.space.create('test')
        -- Names are allowed since 2.11.5 only.
        local msg = "PERSISTENT_NAMES requires schema version 2.11.5"
        t.assert_error_msg_contains(msg, function()
            box.cfg{instance_name = 'test-name'}
        end)
        -- Persistent triggers are allowed only from 3.1.0.
        msg = "PERSISTENT_TRIGGERS requires schema version 3.1.0"
        t.assert_error_msg_contains(msg, function()
            box.schema.func.create('trig', {trigger = 'trg'})
        end)

        box.schema.upgrade()
        -- Now everything is allowed.
        box.space.test:drop()
    end)
end
