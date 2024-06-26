local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')
local fio = require('fio')

local function file_to_lsn(xlog)
    local xlog_name = string.sub(xlog, 1, -6)
    return tonumber(xlog_name)
end

local function xdir_to_xfiles(xdir)
    local files = fio.listdir(xdir)
    local xlogs = {}
    local snap = nil
    for _, file in ipairs(files) do
        if string.match(file, '.xlog') then
            table.insert(xlogs, file)
        elseif string.match(file, '.snap') then
            -- One snap expected since checkpoint_count is 1
            t.assert_equals(snap, nil)
            snap = file
        end
    end
    table.sort(xlogs)
    return xlogs, snap
end

local check_xlog_retry_opts = {delay = 0.8}

local function check_xlog(xdir, lsn)
    -- Retry because xlogs are deleted asynchronously
    t.helpers.retrying(check_xlog_retry_opts, function()
        local xlogs, snap = xdir_to_xfiles(xdir)
        if #xlogs == 0 then
            -- No xlogs - it means that all xlogs were collected, so
            -- simply check if the snap has actual lsn
            t.assert_not_equals(snap, nil)
            t.assert_le(file_to_lsn(snap), lsn)
        elseif #xlogs == 1 then
            t.assert_le(file_to_lsn(xlogs[1]), lsn)
        else
            -- If we have more than two xlog files, we should check if xlog
            -- with the least lsn contains row with required lsn - it means
            -- that the first xlog must have lsn lower or equal than required
            -- one and the second one should have greater lsn.
            t.assert_le(file_to_lsn(xlogs[1]), lsn)
            t.assert_gt(file_to_lsn(xlogs[2]), lsn)
        end
    end)
end

local function check_xlog_ge_no_retry(xdir, lsn)
    local xlogs, snap = xdir_to_xfiles(xdir)
    if #xlogs == 0 then
        -- No xlogs - it means that all xlogs were collected, so
        -- simply check if the snap has actual lsn
        t.assert_not_equals(snap, nil)
        t.assert_ge(file_to_lsn(snap), lsn)
    else
        -- Xlog containing row with required lsn can have lsn
        -- equal to required lsn minus one (then the first row in
        -- it will be required one).
        t.assert_ge(file_to_lsn(xlogs[1]) + 1, lsn)
    end
end

local function check_xlog_ge(xdir, lsn)
    -- Retry because xlogs are deleted asynchronously
    t.helpers.retrying(check_xlog_retry_opts, function()
        check_xlog_ge_no_retry(xdir, lsn)
    end)
end

local function check_xlog_le(xdir, lsn)
    -- Retry because xlogs are deleted asynchronously
    t.helpers.retrying(check_xlog_retry_opts, function()
        local xlogs, snap = xdir_to_xfiles(xdir)
        if #xlogs == 0 then
            -- No xlogs - it means that all xlogs were collected, so
            -- simply check if the snap has actual lsn
            t.assert_not_equals(snap, nil)
            t.assert_le(file_to_lsn(snap), lsn)
        else
            -- Xlog containing row with required lsn can have lsn
            -- equal to required lsn minus one (then the first row in
            -- it will be required one).
            t.assert_lt(file_to_lsn(xlogs[1]), lsn)
        end
    end)
end

local function create_and_fill_space(is_sync)
    local s = box.schema.create_space('test', {is_sync = is_sync})
    s:create_index('pk')
    for i = 1, 10 do
        for j = 1, 10 do
            box.space.test:replace{i, j}
        end
        box.snapshot()
    end
end

local function load_space()
    for i = 1, 10 do
        for j = 1, 10 do
            box.space.test:replace{i, j}
        end
        box.snapshot()
    end
end

local function vclock_sum(vclock)
    local acc = 0
    for _, lsn in pairs(vclock) do
        acc = acc + lsn
    end
    return acc
end

local g = t.group('Persistent gc')

g.before_each(function(cg)
    cg.master = server:new({
        alias = 'master',
        box_cfg = {
            replication_timeout = 0.1,
            checkpoint_count = 1,
            wal_cleanup_delay = 0,
        },
    })
    cg.master:start()

    -- Create replica but don't start it
    local box_cfg = table.deepcopy(cg.master.box_cfg)
    box_cfg.replication = {cg.master.net_box_uri}
    cg.replica = server:new({
        alias = 'replica',
        box_cfg = box_cfg,
    })
end)

g.after_each(function(cg)
    cg.master:drop()
    cg.replica:drop()
end)

g.test_gc_consumer_is_updated_by_relay = function(cg)
    cg.master:exec(create_and_fill_space)
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)

    for _ = 1, 10 do
        -- Make and replicate some writes
        cg.master:exec(load_space)
        -- Since consumers are updated on closing xlog by relay, we
        -- should save vclock of master and make an extra replace to
        -- open a new xlog
        local saved_vclock = cg.master:get_vclock()
        local saved_lsn = vclock_sum(saved_vclock)
        cg.master:exec(function()
            -- Do a checkpoint to write the replace to new xlog
            box.snapshot()
            box.space.test:replace{0}
        end)
        cg.replica:wait_for_vclock_of(cg.master)
        -- Check if gc consumer was actually advanced
        cg.master:exec(function(vclock)
            vclock[0] = nil
            t.helpers.retrying({}, function()
                local consumers = box.space._gc_consumers:select{}
                t.assert_equals(#consumers, 1)
                t.assert_equals(consumers[1].vclock, vclock)
            end)
        end, {saved_vclock})
        -- Trigger snapshot and check if old xlogs were deleted
        cg.master:exec(function() box.space.test:replace{0} end)
        cg.master:exec(function() box.snapshot() end)
        check_xlog_ge(cg.master.workdir, saved_lsn)
    end
end

g.test_gc_consumer_update_retry = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.master:exec(create_and_fill_space)
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)

    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_GC_PERSIST_FIBER', true)
    end)
    local saved_vclock = cg.master:get_vclock()

    -- Make and replicate some writes
    cg.master:exec(load_space)
    local actual_vclock = cg.master:get_vclock()
    -- Since consumers are updated on closing xlog by relay, we
    -- should save vclock of master and make an extra replace to
    -- open a new xlog
    cg.master:exec(function() box.space.test:replace{0} end)
    cg.replica:wait_for_vclock_of(cg.master)
    -- Check if gc consumer was not advanced due to error
    cg.master:exec(function(vclock)
        vclock[0] = nil
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock, vclock)
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(box.info.gc().consumers[1].vclock, vclock)
    end, {saved_vclock})
    cg.master:exec(function()
        box.error.injection.set('ERRINJ_WAL_GC_PERSIST_FIBER', false)
    end)
    -- Check if consumer is advanced after disabling ERRINJ.
    t.helpers.retrying({}, function()
        cg.master:exec(function(vclock)
            vclock[0] = nil
            t.helpers.retrying({}, function()
                local consumers = box.space._gc_consumers:select{}
                t.assert_equals(#consumers, 1)
                t.assert_equals(consumers[1].vclock, vclock)
            end)
        end, {actual_vclock})
    end)
end

g.test_gc_consumer_is_not_updated_without_replica = function(cg)
    cg.master:exec(create_and_fill_space)

    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function()
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock[1], box.info.vclock[1])
    end)

    -- Stop replica and save master vclock and lsn
    cg.replica:stop()
    local saved_vclock = cg.master:get_vclock()
    local saved_lsn = vclock_sum(saved_vclock)

    -- Restart master to check if gc is persisted
    cg.master:restart()
    cg.master:exec(load_space)
    check_xlog(cg.master.workdir, saved_lsn)
    cg.master:exec(function(vclock)
        -- Consumer hasn't changed - replica is disabled
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        vclock[0] = nil
        t.assert_equals(consumers[1].vclock, vclock)
    end, {saved_vclock})
    check_xlog(cg.master.workdir, saved_lsn)
end

-- Check if the replica successfully reconnects if its consumer was
-- manually deactivated
local function persistent_gc_deactivate_consumer_test(cg, prune_logs)
    cg.master:exec(create_and_fill_space)
    local saved_lsn = vclock_sum(cg.master:get_vclock())

    -- Connect replica
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function()
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock[1], box.info.vclock[1])
    end)

    -- Stop replica
    cg.replica:stop()

    cg.master:exec(load_space)
    local lsn_after_load = vclock_sum(cg.master:get_vclock())
    check_xlog(cg.master.workdir, saved_lsn)
    cg.master:exec(function()
        -- Drop consumer
        local replica_uuid = box.space._cluster:select{}[2][2]
        box.internal.deactivate_gc_consumer(replica_uuid)
        t.assert_equals(box.space._gc_consumers:select(), {{replica_uuid}})
        t.assert_equals(box.info.gc().consumers, {})
    end)
    if prune_logs then
        -- Trigger snapshot to prune xlogs
        cg.master:exec(function()
            box.snapshot()
            -- Make sure that consumer is still inactive
            t.assert_equals(box.space._gc_consumers:select()[1].vclock, nil)
            t.assert_equals(box.info.gc().consumers, {})
        end)
        -- Check if old xlogs were deleted
        check_xlog_ge(cg.master.workdir, lsn_after_load)
    end
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function()
        t.assert_equals(#box.space._gc_consumers:select(), 1)
    end)
end

g.test_reconnect_inactive_consumer = function(cg)
    persistent_gc_deactivate_consumer_test(cg, false)
end

g.test_reconnect_inactive_consumer_and_no_logs = function(cg)
    persistent_gc_deactivate_consumer_test(cg, true)
end

g.test_deactivate_gc_consumer_for_connected_replica = function(cg)
    cg.master:exec(create_and_fill_space)

    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        local vclock = consumers[1].vclock

        -- Create dummy replica with consumer to hold xlogs
        box.space._cluster:insert{10, replica_uuid}
        box.space._gc_consumers:replace{replica_uuid, vclock}

        -- Deactivate consumer of actual replica
        box.internal.deactivate_gc_consumer(box.space._cluster:get(2)[2])
    end)
    cg.master:exec(load_space)
    cg.replica:wait_for_vclock_of(cg.master)
    cg.master:exec(function()
        box.space._cluster:delete(10)
        t.assert_not_equals(box.info.gc().consumers, {})
        box.space._cluster:delete(2)
        t.assert_equals(box.space._gc_consumers:select{}, {})
        t.assert_equals(box.info.gc().consumers, {})
    end)
end

g = t.group('Indirect replication of _gc_consumers')

g.before_each(function(cg)
    cg.cluster = cluster:new{}
    local replicaset = {
        server.build_listen_uri('server1', cg.cluster.id),
        server.build_listen_uri('server2', cg.cluster.id),
        server.build_listen_uri('server3', cg.cluster.id)
    }
    local box_cfg = {
        replication_timeout = 0.1,
        checkpoint_count = 1,
        wal_cleanup_delay = 0,
        replication = replicaset,
        election_mode = 'candidate',
        election_timeout = 0.5,
    }
    cg.servers = {}
    for i = 1, 3 do
        local server = cg.cluster:build_and_add_server{
            alias = 'server' .. tostring(i),
            box_cfg = box_cfg,
        }
        table.insert(cg.servers, server)
    end
    cg.cluster:start()
    cg.cluster:wait_for_fullmesh()
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

-- It is important to indirectly replicate consumers for two reasons:
-- 1. After leader change the new one will hold xlogs for replicas
-- 2. Old leader should advance its consumers, otherwise xlogs will live forever
g.test_indirect_replication_of_consumers = function(cg)
    local leader = cg.cluster:get_leader()
    leader:exec(create_and_fill_space, {true})
    for _, s in pairs(cg.servers) do
        s:wait_for_vclock_of(leader)
    end

    -- Stop current leader
    local saved_lsn = vclock_sum(leader:get_vclock())
    leader:stop()
    cg.cluster:delete_server(leader.alias)

    -- Get new leader
    local old_leader = leader
    t.helpers.retrying({}, function(cg)
        t.assert_not_equals(cg.cluster:get_leader(), nil)
    end, cg)
    leader = cg.cluster:get_leader()
    leader:exec(load_space)
    for _, s in pairs(cg.cluster.servers) do
        s:wait_for_vclock_of(leader)
        s:exec(function() box.snapshot() end)
        check_xlog_le(s.workdir, saved_lsn)
    end

    -- Enable the old leader back
    cg.cluster:add_server(old_leader)
    old_leader:start()
    cg.cluster:wait_for_fullmesh()
    leader:exec(load_space)
    -- Check if old xlogs of all replicas will be cleaned up
    for _, s in pairs(cg.cluster.servers) do
        -- Trigger snapshot on each replica to create a new checkpoint
        s:wait_for_vclock_of(leader)
        -- Save vclock of replica to check xlog later against it
        -- We cannot rely on vclock of master because the replica has
        -- independent local dimension that is a part of lsn
        -- It is important to save it before cleanup because consumers
        -- can bump local lsn in background
        s.saved_vclock = s:get_vclock()
        s:exec(function() box.snapshot() end)
    end
    leader:exec(function()
        for i = 1, 10 do
            box.space.test:replace{i}
        end
        box.snapshot()
    end)
    -- Wait for the replace to appear on all replicas
    for _, s in pairs(cg.cluster.servers) do
        s:wait_for_vclock_of(leader)
    end
    for _, s in pairs(cg.cluster.servers) do
        -- We need to retry here because consumers are updated asynchronously
        t.helpers.retrying({timeout = 20, delay = 1}, function()
            -- Trigger snapshot to collect xlogs
            s:exec(function() box.snapshot() end)
            check_xlog_ge_no_retry(s.workdir, vclock_sum(s.saved_vclock))
        end)
        s.saved_vclock = nil
    end
end
