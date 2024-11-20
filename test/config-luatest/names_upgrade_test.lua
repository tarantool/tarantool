local t = require('luatest')
local treegen = require('luatest.treegen')
local server = require('luatest.server')
local yaml = require('yaml')
local fun = require('fun')
local fio = require('fio')

local g_2_11_0 = t.group('upgrade-from-2.11.0')
local g_2_11_5 = t.group('upgrade-from-2.11.5')

-- Helper, which sets assert_names_after_upgrade function on instance.
local function set_assert_names_after_upgrade()
    rawset(_G, 'assert_names_after_upgrade',
        function(instance_name, replicaset_name, names)
            local info = box.info
            t.assert_equals(info.name, instance_name)
            t.assert_equals(info.replicaset.name, replicaset_name)

            local alerts = require('config'):info().alerts
            for name, _ in pairs(names) do
                t.assert_equals(alerts[name], nil)
            end
        end
    )
end

local function init_replicaset(cg, uuids, datadir_prefix)
    cg.uuids = uuids
    local datadir_1 = fio.abspath(fio.pathjoin(datadir_prefix, 'instance-001'))
    local datadir_2 = fio.abspath(fio.pathjoin(datadir_prefix, 'instance-002'))

    local dir = treegen.prepare_directory({}, {})
    local workdir_1 = fio.pathjoin(dir, 'instance-001')
    local workdir_2 = fio.pathjoin(dir, 'instance-002')

    fio.mktree(workdir_1)
    fio.mktree(workdir_2)

    fio.copytree(datadir_1, workdir_1)
    fio.copytree(datadir_2, workdir_2)

    local config = {
        credentials = {
            users = {
                guest = {
                    roles = {'super'},
                },
            },
        },

        iproto = {
            listen = {{uri = 'unix/:./{{ instance_name }}.iproto'}},
        },

        groups = {
            ['group-001'] = {
                replicasets = {
                    ['replicaset-001'] = {
                        database = {
                            replicaset_uuid = uuids['replicaset-001']
                        },
                        instances = {
                            ['instance-001'] = {
                                snapshot = { dir = workdir_1, },
                                wal = { dir = workdir_1, },
                                database = {
                                    instance_uuid = uuids['instance-001'],
                                    mode = 'rw',
                                },
                            },
                            ['instance-002'] = {
                                snapshot = { dir = workdir_2, },
                                wal = { dir = workdir_2, },
                                database = {
                                    instance_uuid = uuids['instance-002']
                                },
                            },
                        },
                    },
                },
            },
        },
    }

    local cfg = yaml.encode(config)
    local config_file = treegen.write_file(dir, 'cfg.yaml', cfg)
    local opts = {config_file = config_file, chdir = dir}
    cg.instance_1 =
        server:new(fun.chain(opts, {alias = 'instance-001'}):tomap())
    cg.instance_2 =
        server:new(fun.chain(opts, {alias = 'instance-002'}):tomap())

    cg.instance_1:start({wait_until_ready = false})
    cg.instance_2:start({wait_until_ready = false})
    cg.instance_1:wait_until_ready()
    cg.instance_2:wait_until_ready()

    for _, server_name in ipairs({'instance_1', 'instance_2'}) do
        cg[server_name]:exec(set_assert_names_after_upgrade)
    end
end

local function drop_replicaset(cg)
    cg.instance_1:drop()
    cg.instance_2:drop()
end

local function assert_names_before_upgrade()
    local info = box.info
    t.assert_equals(info.name, nil)
    t.assert_equals(info.replicaset.name, nil)
    -- Check that config:reload() respects old schema version.
    local config = require('config')
    config:reload()
    info = box.info
    t.assert_equals(info.name, nil)
    t.assert_equals(info.replicaset.name, nil)
end

local function wait_names(instance_name, replicaset_name, names)
    t.helpers.retrying({timeout = 20}, function()
        _G.assert_names_after_upgrade(instance_name, replicaset_name, names)
    end)
end

g_2_11_0.before_all(function(g)
    local uuids = {
        ['replicaset-001'] = 'cbf06940-0790-498b-948d-042b62cf3d29',
        ['instance-001']   = '8a274925-a26d-47fc-9e1b-af88ce939412',
        ['instance-002']   = '3de2e3e1-9ebe-4d0d-abb1-26d301b84633'
    }
    local datadir_prefix = 'test/box-luatest/upgrade/2.11.0/replicaset/'
    init_replicaset(g, uuids, datadir_prefix)
end)

g_2_11_0.after_all(function(g)
    drop_replicaset(g)
end)

--
-- The test checks the following user scenario:
--   1. User upgrades instances from 2.11.0 to the latest;
--   2. Names are automatically set as soon as it's possible (on schema 2.11.5);
--   3. User downgrades back to the 2.11.0, names remain set.
--
g_2_11_0.test_upgrade = function(g)
    g.instance_1:exec(assert_names_before_upgrade)
    g.instance_2:exec(assert_names_before_upgrade)

    g.instance_1:exec(function()
        box.ctl.wait_rw()
        t.assert_equals(box.space._schema:get{'version'}, {'version', 2, 11, 0})
        -- Stop upgrade process in the middle, right after upgrading to 2.11.5,
        -- since it's the first 2.11 release, which properly works with names.
        box.internal.run_schema_upgrade(function()
            box.space._schema:delete("max_id")
            box.space._schema:replace{'version', 2, 11, 5}
        end)
    end)

    local rs = 'replicaset-001'
    local i1 = g.instance_1.alias
    local i2 = g.instance_2.alias
    g.instance_1:exec(wait_names, {i1, rs, g.uuids})
    g.instance_2:exec(wait_names, {i2, rs, g.uuids})

    -- Finish upgrade process. Names are still set.
    g.instance_1:exec(function()
        box.schema.upgrade()
    end)
    g.instance_1:call('_G.assert_names_after_upgrade', {i1, rs, g.uuids})
    g.instance_2:call('_G.assert_names_after_upgrade', {i2, rs, g.uuids})

    -- Downgrade back to the 2.11.0. Names remain set, since it's allowed to
    -- downgrade with names. Note, that it'll be impossible to drop names
    -- from the _cluster space before 2.11.5 due to the #10549 issue.
    g.instance_1:exec(function()
        box.schema.downgrade('2.11.0')
    end)
    g.instance_1:call('_G.assert_names_after_upgrade', {i1, rs, g.uuids})
    g.instance_2:call('_G.assert_names_after_upgrade', {i2, rs, g.uuids})
end

g_2_11_5.before_all(function(g)
    local uuids = {
        ['replicaset-001'] = 'b5c6e102-aa65-4b5f-a967-ee2a4f5d1480',
        ['instance-001']   = '22188ab6-e0e5-484c-89b6-5dfe2b969b7b',
        ['instance-002']   = '30d791cf-3d88-4a62-81d6-9fe5e45dc44c',
    }
    local datadir_prefix = 'test/box-luatest/upgrade/2.11.5/replicaset/'
    init_replicaset(g, uuids, datadir_prefix)
end)

g_2_11_5.after_all(function(g)
    drop_replicaset(g)
end)

--
-- The test checks the following user scenario:
--   1. User wants to upgrade instances from 2.11.5 to the latest.
--      Persistent names are initialized before upgrade is done.
--   3. User upgrades and then downgrades back, names remain set.
--
g_2_11_5.test_upgrade = function(g)
    local rs = 'replicaset-001'
    local i1 = g.instance_1.alias
    local i2 = g.instance_2.alias
    g.instance_1:exec(wait_names, {i1, rs, g.uuids})
    g.instance_2:exec(wait_names, {i2, rs, g.uuids})

    g.instance_1:exec(function()
        box.schema.upgrade()
    end)
    g.instance_1:call('_G.assert_names_after_upgrade', {i1, rs, g.uuids})
    g.instance_2:call('_G.assert_names_after_upgrade', {i2, rs, g.uuids})

    g.instance_1:exec(function()
        -- Since 2.11.5 is not yet released, it's impossible to downgrade to
        -- 2.11.5, so we downgrade to 2.11.4.
        box.schema.downgrade('2.11.4')
    end)
    g.instance_1:call('_G.assert_names_after_upgrade', {i1, rs, g.uuids})
    g.instance_2:call('_G.assert_names_after_upgrade', {i2, rs, g.uuids})
end
