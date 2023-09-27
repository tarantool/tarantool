local server = require('luatest.server')
local t = require('luatest')

local g = t.group("Upgrade to 3.0.0")

g.before_each(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/2.11.0',
    })
    cg.server:start()
    cg.server:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
    end)
end)

g.after_each(function(cg)
    cg.server:stop()
end)

g.test_new_replicaset_uuid_key = function(cg)
    cg.server:exec(function()
        box.schema.upgrade()
        local _schema = box.space._schema
        t.assert_equals(_schema:get{'cluster'}, nil)
        t.assert_equals(_schema:get{'replicaset_uuid'}.value,
                        box.info.replicaset.uuid)
    end)
end

--
-- Test, that we're able to start with snaps, which doesn't include
-- name in it, but does in cfg. Test, that names are correctly applied
-- after second box.cfg.
--
g.test_start_with_old_snaps = function(cg)
    local cfg = {
        instance_name = 'instance',
        replicaset_name = 'replicaset',
        cluster_name = 'cluster',
    }

    cg.server:restart({box_cfg = cfg})
    cg.server:exec(function(cfg)
        local info = box.info
        t.assert_equals(info.name, nil)
        t.assert_equals(info.replicaset.name, nil)
        t.assert_equals(info.cluster.name, nil)
        box.schema.upgrade()
        --
        -- After schema upgrade names must be set one more time, if
        -- user didn't apply names via config module.
        --
        box.cfg(cfg)
        info = box.info
        t.assert_equals(info.name, cfg.instance_name)
        t.assert_equals(info.replicaset.name, cfg.replicaset_name)
        t.assert_equals(info.cluster.name, cfg.cluster_name)
    end, {cfg})
end
