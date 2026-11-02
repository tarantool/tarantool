local t = require('luatest')
local server = require('luatest.server')
local fio = require('fio')
local socket = require('socket')
local xlog = require('xlog')

local g = t.group()

g.before_all(function(cg)
    cg.master = server:new({alias = 'master'})
    cg.master:start()
    cg.master:exec(function()
        local s = box.schema.space.create('master_data')
        s:create_index('pk')
        s:insert{1, 'from master'}
        box.cfg{bootstrap_strategy = 'supervised'}
        box.ctl.make_bootstrap_leader()
        box.cfg{read_only = true}
    end)
    cg.replica_admin = server.build_listen_uri('replica_admin')
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = {
            -- Self-bootstrap must happen only after the failed JOIN.
            bootstrap_strategy = 'supervised',
            replication = {cg.master.net_box_uri},
            replication_connect_timeout = 120,
            replication_timeout = 0.1,
            force_recovery = false,
        },
        env = {
            TARANTOOL_RUN_BEFORE_BOX_CFG =
                ("require('console').listen(%q)"):format(
                    'unix/:' .. cg.replica_admin),
        },
    })
end)

g.after_all(function(cg)
    cg.replica:drop()
    cg.master:drop()
end)

local function make_bootstrap_leader(admin_uri)
    local admin = socket.tcp_connect('unix/', admin_uri)
    t.assert(admin)
    t.assert_str_contains(admin:read(128, 120), 'Lua console')
    admin:write('box.ctl.make_bootstrap_leader()\n', 120)
    t.assert_equals(admin:read('...\n', 120), '---\n...\n')
    admin:close()
end

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

g.test_bootstrap_after_failed_join = function(cg)
    cg.replica:start({wait_until_ready = false})
    t.helpers.retrying({timeout = 120}, function()
        t.assert(cg.replica:grep_log('READONLY'))
        t.assert(cg.replica:grep_log('rebootstrap failed, will retry'))
    end)
    local master_replicaset_uuid = cg.master:exec(function()
        return box.info.replicaset.uuid
    end)
    make_bootstrap_leader(cg.replica_admin)
    cg.replica:wait_until_ready()
    cg.master:stop()

    local instance_uuid, replicaset_uuid = cg.replica:exec(function()
        box.cfg{replication = {}}
        box.ctl.wait_rw(120)
        t.assert_equals(box.space._cluster:select(), {{1, box.info.uuid}})
        t.assert_equals(box.space.master_data, nil)
        return box.info.uuid, box.info.replicaset.uuid
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
        t.assert_equals(signature, vclock_sum(xlog.meta(path).vclock))
    end

    cg.replica.box_cfg.replication = {}
    cg.replica:restart()
    cg.replica:exec(function(instance_uuid, replicaset_uuid)
        t.assert_not(box.cfg.force_recovery)
        t.assert_not(box.info.ro)
        t.assert_equals(box.info.uuid, instance_uuid)
        t.assert_equals(box.info.replicaset.uuid, replicaset_uuid)
        t.assert_equals(box.space._cluster:select(), {{1, instance_uuid}})
        t.assert_equals(box.space.master_data, nil)
        t.assert_equals(box.space.test:select(), {{1, 'after bootstrap'}})
    end, {instance_uuid, replicaset_uuid})
    assert_valid_vclock(cg.replica)
end
