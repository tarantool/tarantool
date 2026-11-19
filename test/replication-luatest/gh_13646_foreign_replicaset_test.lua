local t = require('luatest')
local server = require('luatest.server')
local fio = require('fio')
local net_box = require('net.box')

local uuid_a = t.helpers.uuid('a')
local uuid_b = t.helpers.uuid('b')
local instance_uuid = t.helpers.uuid('c')

local function new_server(cg, alias, cfg)
    local s = server:new({alias = alias, box_cfg = cfg})
    table.insert(cg.servers, s)
    return s
end

local function seed(s, name, count)
    return s:exec(function(name, count)
        local space = box.schema.space.create(name)
        space:create_index('pk')
        for i = 1, count do
            space:insert{i, name}
        end
        return box.info.vclock[1]
    end, {name, count})
end

local function before_each(cg)
    cg.servers = {}
    cg.foreign = new_server(cg, 'foreign', {
        replicaset_uuid = uuid_b,
        checkpoint_count = 1,
        wal_cleanup_delay = 0,
    })
    cg.foreign:start()
    seed(cg.foreign, 'foreign_data', 100)
    cg.foreign:exec(function()
        -- Publish the advanced vclock before the next instance connects.
        box.cfg{read_only = true}
        box.cfg{read_only = false}
    end)
end

local function after_each(cg)
    if cg.connection ~= nil then
        cg.connection:close()
    end
    for i = #cg.servers, 1, -1 do
        cg.servers[i]:drop()
    end
end

local function assert_mismatch(s)
    t.helpers.retrying({timeout = 120}, function()
        t.assert(s:grep_log('REPLICASET_UUID_MISMATCH'))
    end)
end

local function assert_no_foreign_registration(cg)
    cg.foreign:exec(function()
        t.assert_equals(box.space._cluster:count(), 1)
    end)
end

local g_recovery = t.group('foreign_replicaset_recovery', {
    {configured = true, gc = false},
    {configured = true, gc = true},
    {configured = false, gc = false},
})

g_recovery.before_each(before_each)
g_recovery.after_each(after_each)

g_recovery.test_recovery = function(cg)
    local s = new_server(cg, 'local', {
        replicaset_uuid = cg.params.configured and uuid_a or nil,
        replication_connect_timeout = 120,
    })
    s:start()
    local local_lsn = seed(s, 'local_data', 1)
    local local_uuid = s:exec(function()
        box.snapshot()
        return box.info.replicaset.uuid
    end)
    if cg.params.gc then
        cg.foreign:exec(function(local_lsn)
            box.snapshot()
            t.helpers.retrying({timeout = 120}, function()
                t.assert_gt(box.info.gc().vclock[1], local_lsn)
            end)
            box.cfg{read_only = true}
            box.cfg{read_only = false}
        end, {local_lsn})
    end
    s.box_cfg.replication = {cg.foreign.net_box_uri}
    s:restart()
    assert_mismatch(s)
    s:exec(function(local_uuid)
        box.cfg{replication = {}}
        t.assert_not(box.info.ro)
        t.assert_equals(box.info.replicaset.uuid, local_uuid)
        t.assert_equals(box.space.local_data:select(), {{1, 'local_data'}})
        t.assert_equals(box.space.foreign_data, nil)
    end, {local_uuid})
    assert_no_foreign_registration(cg)
    s.box_cfg.replication = {}
    s:restart()
    s:exec(function(local_uuid)
        t.assert_not(box.info.ro)
        t.assert_equals(box.info.replicaset.uuid, local_uuid)
        t.assert_equals(box.space.local_data:select(), {{1, 'local_data'}})
    end, {local_uuid})
end

local g = t.group('foreign_replicaset')
g.before_each(before_each)
g.after_each(after_each)

local function test_bootstrap(cg)
    local s = new_server(cg, 'local', {
        replicaset_uuid = uuid_a,
        replication = {cg.foreign.net_box_uri},
        replication_connect_timeout = 120,
    })
    s:start({wait_until_ready = false})
    assert_mismatch(s)
    t.helpers.retrying({timeout = 120}, function()
        t.assert_not(s.process:is_alive())
    end)
    t.assert_equals(fio.glob(fio.pathjoin(s.workdir, '*.snap')), {})
    assert_no_foreign_registration(cg)
    s.box_cfg.replication = {}
    s:restart()
    s:exec(function(uuid)
        t.assert_equals(box.info.replicaset.uuid, uuid)
        t.assert_equals(box.space.foreign_data, nil)
    end, {uuid_a})
end

g.test_bootstrap = test_bootstrap

g.test_bootstrap_with_vote = function(cg)
    cg.foreign:exec(function()
        -- Without feature negotiation the applier falls back to VOTE.
        box.iproto.override(box.iproto.type.ID, function()
            box.error(box.error.UNKNOWN_REQUEST_TYPE, box.iproto.type.ID)
        end)
    end)
    test_bootstrap(cg)
end

g.test_advertise_uuid_before_bootstrap = function(cg)
    local s = new_server(cg, 'local', {
        replicaset_uuid = uuid_a,
        bootstrap_strategy = 'supervised',
        replication_connect_timeout = 120,
    })
    s:start({wait_until_ready = false})
    cg.connection = net_box.connect(s.net_box_uri, {
        wait_connected = false,
        reconnect_after = 0.1,
        fetch_schema = false,
    })
    local ballot
    cg.connection:watch('internal.ballot', function(_, value)
        ballot = value and value[box.iproto.key.BALLOT]
    end)
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.connection:is_connected(), cg.connection.error)
        t.assert(ballot)
        t.assert_equals(ballot[box.iproto.ballot_key.REPLICASET_UUID], uuid_a)
        t.assert_not(ballot[box.iproto.ballot_key.IS_BOOTED])
    end)
end

g.test_rename = function(cg)
    local master = new_server(cg, 'master', {replicaset_uuid = uuid_a})
    master:start()
    seed(master, 'local_data', 1)
    local replica = new_server(cg, 'replica', {
        replication = {master.net_box_uri},
        read_only = true,
        replication_connect_timeout = 120,
    })
    replica:start()
    replica:assert_follows_upstream(1)
    replica:exec(function(uris)
        box.cfg{replication = uris}
        box.cfg{instance_name = 'renamed'}
        t.assert_equals(box.info.name, 'renamed')
        t.assert_equals(box.space.local_data:select(), {{1, 'local_data'}})
    end, {{master.net_box_uri, cg.foreign.net_box_uri}})
    assert_mismatch(replica)
    assert_no_foreign_registration(cg)
    replica.box_cfg.replication = {master.net_box_uri}
    replica:restart()
    replica:exec(function()
        t.assert_equals(box.space.local_data:select(), {{1, 'local_data'}})
    end)
end

g.test_uuid_assigned_after_connect = function(cg)
    local s = new_server(cg, 'local', {
        bootstrap_strategy = 'config',
        bootstrap_leader = instance_uuid,
        instance_uuid = instance_uuid,
        replication = {cg.foreign.net_box_uri},
        replication_connect_timeout = 120,
    })
    s:start()
    t.assert(s:grep_log('connected to 1 replicas'))
    assert_mismatch(s)
    s:exec(function(uuid)
        t.assert_not_equals(box.info.replicaset.uuid, uuid)
        t.assert_not(box.info.ro)
        t.assert_equals(box.space.foreign_data, nil)
        local space = box.schema.space.create('local_data')
        space:create_index('pk')
        space:insert{1}
    end, {uuid_b})
    assert_no_foreign_registration(cg)
end
