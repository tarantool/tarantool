local server = require('luatest.server')
local replica_set = require('luatest.replica_set')
local t = require('luatest')

local g = t.group("Upgrade from 2.11.0")

g.before_all(function(cg)
    cg.replica_set = replica_set:new{}
    cg.master = cg.replica_set:build_and_add_server({
        alias = 'master',
        datadir = 'test/box-luatest/upgrade/2.11.0/replicaset/instance-001'
    })
    cg.replica = cg.replica_set:build_and_add_server({
        alias = 'replica',
        datadir = 'test/box-luatest/upgrade/2.11.0/replicaset/instance-002',
        box_cfg = {
            read_only = true,
            replication = cg.master.net_box_uri,
        },
    })
    cg.replica_set:start()
end)

g.after_all(function(cg)
    cg.replica_set:drop()
end)

g.after_each(function(cg)
    cg.master:exec(function()
        box.schema.downgrade('2.11.0')
        box.snapshot()
    end)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.replica:exec(function()
        box.snapshot()
    end)
end)

g.test_new_replicaset_uuid_key = function(cg)
    cg.master:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
        box.schema.upgrade()

        local _schema = box.space._schema
        t.assert_equals(_schema:get{'cluster'}, nil)
        t.assert_equals(_schema:get{'replicaset_uuid'}.value,
                        box.info.replicaset.uuid)
    end)
end

g.test_ddl_is_forbidden = function(cg)
    cg.master:exec(function()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
        -- DDL is prohibited before 2.11.1.
        local msg = "DDL_BEFORE_UPGRADE requires schema version 2.11.1"
        t.assert_error_msg_contains(msg, function()
            box.schema.space.create('test')
        end)

        box.schema.upgrade()
    end)
end

--
-- gh-10546: check, that dd_version is properly broadcasted, when
-- schema version is changed.
--
g.test_dd_version_broadcast = function(cg)
    local function set_dd_version_watcher()
        local version = require('version')
        rawset(_G, 'watcher_counter', 0)
        rawset(_G, 'watcher_version', {})
        rawset(_G, 'watcher', box.watch('box.status', function(_, status)
            t.assert_not_equals(status.dd_version, nil)
            _G.watcher_version = version.fromstr(status.dd_version)
            _G.watcher_counter = _G.watcher_counter + 1
        end))
    end

    local function clear_dd_version_watcher()
        _G.watcher:unregister()
    end

    cg.master:exec(set_dd_version_watcher)
    cg.replica:exec(set_dd_version_watcher)

    cg.master:exec(function()
        local old_counter = _G.watcher_counter
        box.schema.upgrade()
        t.helpers.retrying({}, function()
            t.assert_gt(_G.watcher_counter, old_counter)
        end)
        t.assert_equals(_G.watcher_version, box.internal.latest_dd_version())
    end)

    cg.replica:wait_for_vclock_of(g.master)
    cg.replica:exec(function()
        t.assert_equals(_G.watcher_version, box.internal.latest_dd_version())
    end)

    cg.master:exec(clear_dd_version_watcher)
    cg.replica:exec(clear_dd_version_watcher)
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
