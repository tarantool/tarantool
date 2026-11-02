local t = require('luatest')
local server = require('luatest.server')
local fio = require('fio')
local yaml = require('yaml')

local g = t.group()

g.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.master:exec(function()
        local s = box.schema.space.create('master_data')
        s:create_index('pk')
        s:insert{1, 'from master'}
        box.cfg{read_only = true}
    end)
    cg.replica = server:new({
        alias = 'replica',
        net_box_uri = server.build_listen_uri('replica'),
        box_cfg = {
            -- Prefer the booted master until it stops after the failed JOIN.
            -- Then the connection to self is sufficient to bootstrap locally.
            bootstrap_strategy = 'legacy',
            replication = {
                cg.master.net_box_uri,
                server.build_listen_uri('replica'),
            },
            replication_connect_quorum = 1,
            replication_connect_timeout = 1,
            replication_timeout = 0.1,
            force_recovery = false,
        },
    })
end)

g.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

local function vclock_sum(vclock)
    local sum = 0
    for _, lsn in pairs(vclock) do
        sum = sum + lsn
    end
    return sum
end

local function assert_valid_vclock(replica)
    local signature, vclock = replica:exec(function()
        return box.info.signature, box.info.vclock
    end)
    t.assert_equals(signature, vclock_sum(vclock))
end

-- 2.11 has no xlog.meta(), so read VClock from the textual snapshot header.
local function snapshot_vclock(path)
    local file = assert(io.open(path, 'rb'))
    local vclock
    for line in file:lines() do
        if line == '' then
            break
        end
        local value = line:match('^VClock: (.*)$')
        if value then
            vclock = yaml.decode(value)
        end
    end
    file:close()
    t.assert_type(vclock, 'table')
    return vclock
end

g.test_bootstrap_after_failed_join = function(cg)
    cg.replica:start({wait_until_ready = false})
    local logfile = fio.pathjoin(cg.replica.workdir, 'replica.log')
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica:grep_log('ER_READONLY', nil, {filename = logfile}))
        t.assert(cg.replica:grep_log('rebootstrap failed, will retry', nil,
                                    {filename = logfile}))
    end)
    local master_replicaset_uuid = cg.master:exec(function()
        return box.info.cluster.uuid
    end)
    cg.master:stop()
    cg.replica:wait_until_ready()

    local instance_uuid, replicaset_uuid = cg.replica:exec(function()
        box.cfg{replication = {}}
        box.ctl.wait_rw(120)
        t.assert_equals(box.space._cluster:select(), {{1, box.info.uuid}})
        t.assert_equals(box.space.master_data, nil)
        return box.info.uuid, box.info.cluster.uuid
    end)
    t.assert_not_equals(replicaset_uuid, master_replicaset_uuid)
    assert_valid_vclock(cg.replica)

    cg.replica:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
        s:insert{1, 'after bootstrap'}
        box.snapshot()
    end)
    assert_valid_vclock(cg.replica)
    local snapshots = fio.glob(fio.pathjoin(cg.replica.workdir, '*.snap'))
    t.assert_equals(#snapshots, 2)
    for _, path in ipairs(snapshots) do
        local signature = tonumber(fio.basename(path, '.snap'))
        t.assert_equals(signature, vclock_sum(snapshot_vclock(path)))
    end

    cg.replica.box_cfg.replication = {}
    cg.replica:restart()
    cg.replica:exec(function(instance_uuid, replicaset_uuid)
        t.assert_not(box.cfg.force_recovery)
        t.assert_not(box.info.ro)
        t.assert_equals(box.info.uuid, instance_uuid)
        t.assert_equals(box.info.cluster.uuid, replicaset_uuid)
        t.assert_equals(box.space._cluster:select(), {{1, instance_uuid}})
        t.assert_equals(box.space.master_data, nil)
        t.assert_equals(box.space.test:select(), {{1, 'after bootstrap'}})
    end, {instance_uuid, replicaset_uuid})
    assert_valid_vclock(cg.replica)
end
