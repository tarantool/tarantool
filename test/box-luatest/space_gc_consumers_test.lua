local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')

local g = t.group('Upgrade', {{version = '2.11.0'}, {version = '3.0.0'}})

local WARNING_PATTERN = 'W> Current schema does not support persistent ' ..
    'WAL GC state, so wal_cleanup_delay option'

g.before_each(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/' .. cg.params.version,
        box_cfg = {wal_cleanup_delay = 0, log_level = 4}
    })
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_upgrade = function(cg)
    cg.server:exec(function()
        local _priv = box.space._priv
        local space_id = box.schema.GC_CONSUMERS_ID

        t.assert_equals(box.space._space:get(space_id), nil)
        t.assert_equals(_priv.index.object:select{'space', space_id}, {})

        box.schema.upgrade()

        t.assert_not_equals(box.space._space:get(space_id), nil)
        t.assert_equals(#_priv.index.object:select{'space', space_id}, 1)
    end)
    t.assert_not(cg.server:grep_log(WARNING_PATTERN))
end

g = t.group('Cluster upgrade', {{version = '2.11.0'}, {version = '3.0.0'}})

g.before_each(function(cg)
    cg.cluster = cluster:new{}
    local replicaset = {
        server.build_listen_uri('server1', cg.cluster.id),
        server.build_listen_uri('server2', cg.cluster.id),
        server.build_listen_uri('server3', cg.cluster.id)
    }
    local box_cfg = {
        replication_timeout = 0.1,
        replication = replicaset,
        election_mode = 'candidate',
        wal_cleanup_delay = 0,
        log_level = 4,
    }
    cg.servers = {}
    for i = 1, 3 do
        local datadir =
            string.format('test/box-luatest/upgrade/%s-cluster/server%d',
                          cg.params.version, i)
        local server = cg.cluster:build_and_add_server{
            alias = 'server' .. tostring(i),
            datadir = datadir,
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

g.test_cluster_upgrade = function(cg)
    -- Zero wal_cleanup_delay value should be replaced with default
    for _, s in pairs(cg.servers) do
        t.assert(s:grep_log(WARNING_PATTERN))
    end

    t.helpers.retrying({}, function(cg)
        t.assert_not_equals(cg.cluster:get_leader(), nil)
    end, cg)
    local leader = cg.cluster:get_leader()

    leader:exec(function()
        box.schema.upgrade()
    end)

    -- Wait for all replicas to be upgraded
    for _, s in pairs(cg.servers) do
        s:wait_for_vclock_of(leader)
    end

    -- Check if all gc consumers are present on all replicas
    for _, s in pairs(cg.servers) do
        s:exec(function(servers_num)
            -- No gc consumer for self
            local consumers_num = servers_num - 1
            t.assert_equals(#box.space._gc_consumers:select(), consumers_num)
        end, {#cg.servers})
        t.assert(s:grep_log(WARNING_PATTERN))
    end
end

g = t.group('Space _gc_consumers')

local function create_and_fill_space()
    local s = box.schema.create_space('test')
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

g.before_each(function(cg)
    cg.master = server:new({
        alias = 'master',
        box_cfg = {
            wal_cleanup_delay = 0,
            memtx_use_mvcc_engine = true,
        },
    })
    cg.master:start()
end)

g.after_each(function(cg)
    cg.master:drop()
end)

g.test_gc_consumers_invalid_data = function(cg)
    cg.master:exec(function()
        local correct_uuid = require('uuid').str()
        local vclock = box.info.vclock

        -- UUID
        t.assert_error_msg_equals("Invalid UUID: abc",
            function() box.space._gc_consumers:insert{'abc', vclock} end)
        vclock[100] = 10

        -- vclock
        t.assert_error_msg_equals("Invalid vclock",
            function() box.space._gc_consumers:insert{correct_uuid, vclock} end)
        vclock = box.info.vclock
        vclock.k = 'v'
        t.assert_error_msg_equals("Invalid vclock",
            function() box.space._gc_consumers:insert{correct_uuid, vclock} end)
        vclock = box.info.vclock
        vclock[1] = -1
        t.assert_error_msg_equals("Invalid vclock",
            function() box.space._gc_consumers:insert{correct_uuid, vclock} end)

        -- opts
        vclock = box.info.vclock
        local opts = {non_existing_opt = 'v'}
        t.assert_error_msg_equals("unexpected option 'non_existing_opt'",
            function()
                box.space._gc_consumers:insert{correct_uuid, vclock, opts}
            end)
    end)
end

local function gc_consumers_basic_test(cg, restart)
    cg.master:exec(create_and_fill_space)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local vclock = box.info.vclock
        vclock[0] = nil
        box.space._gc_consumers:replace{replica_uuid, vclock}
    end)
    local saved_vclock = cg.master:get_vclock()

    -- Restart master to check if gc is persisted
    if restart then
        cg.master:restart()
    end

    cg.master:exec(load_space)
    cg.master:exec(function(vclock)
        -- Check if consumer hasn't changed
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        vclock[0] = nil
        t.assert_equals(consumers[1].vclock, vclock)

        -- Check consumer object with info
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(box.info.gc().consumers[1].vclock, vclock)
    end, {saved_vclock})
end

g.test_gc_consumers_basic = function(cg)
    gc_consumers_basic_test(cg, false)
end

g.test_gc_consumers_basic_persist = function(cg)
    gc_consumers_basic_test(cg, true)
end

-- Test if outdated consumer has no effect on GC.
local function gc_consumers_outdated_consumer_test(cg, restart)
    cg.master:exec(create_and_fill_space)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local vclock = box.info.vclock
        vclock[1] = 1
        box.space._gc_consumers:replace{replica_uuid, vclock}
        t.assert_equals(box.info.gc().consumers, {})
    end)

    -- Restart master to check if gc is persisted
    if restart then
        cg.master:restart()
    end

    cg.master:exec(load_space)
    cg.master:exec(function()
        -- Check if consumer hasn't changed
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock[1], 1)

        t.assert_equals(box.info.gc().consumers, {})
    end)
end

g.test_gc_consumers_outdated_consumer = function(cg)
    gc_consumers_outdated_consumer_test(cg, false)
end

g.test_gc_consumers_outdated_consumer_persist = function(cg)
    gc_consumers_outdated_consumer_test(cg, true)
end

g.test_gc_consumers_on_integration_with_cluster = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}

        -- Check that dummy consumer is created along with replica
        -- and it does not pin any xlogs.
        t.assert_equals(#box.space._gc_consumers:select{}, 1)
        t.assert_equals(box.space._gc_consumers:get(replica_uuid)[2], nil)
        t.assert_equals(box.info.gc().consumers, {})

        -- Create actual consumer for replica
        box.space._gc_consumers:replace{replica_uuid, box.info.vclock}
        t.assert_equals(#box.info.gc().consumers, 1)

        -- Check if consumer of replica cannot be dropped
        local errmsg = 'gc_consumer does not support delete while its ' ..
            'replica is still registered'
        t.assert_error_msg_equals(errmsg,
            function() box.space._gc_consumers:delete(replica_uuid) end)

        -- Check if consumer is dropped along with the replica.
        box.space._cluster:delete(10)
        t.assert_equals(box.space._gc_consumers:select{}, {})
        t.assert_equals(box.info.gc().consumers, {})
        box.commit()
    end)
end

-- Check if consumer is deleted on replica uuid update
g.test_gc_consumers_on_replica_uuid_update = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        local new_replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid, 'name'}
        box.space._gc_consumers:replace{replica_uuid, box.info.vclock}

        box.space._cluster:replace{10, new_replica_uuid, 'name'}
        -- Consumer of old replica must be replaced with dummy consumer
        -- of the new one.
        t.assert_equals(box.space._gc_consumers:select(), {{new_replica_uuid}})
    end)
end

g.test_gc_consumers_on_replica_name_update = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local consumer =
            box.space._gc_consumers:replace{replica_uuid, box.info.vclock}

        box.space._cluster:replace{10, replica_uuid, 'name'}
        -- Consumer should stay intact after populating replica with name.
        t.assert_equals(box.space._gc_consumers:select(), {consumer})
    end)
end

-- The test checks if the consumer is deleted on commit, not replace
g.test_gc_consumers_unregister_late = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}

        local vclock = box.info.vclock
        vclock[0] = nil
        vclock[1] = math.random(100, 1000)
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.begin()
        -- Set dummmy consumer that is inactive right from the start
        box.space._gc_consumers:replace{replica_uuid}
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(box.info.gc().consumers[1].vclock, vclock)
        box.commit()
        t.assert_equals(box.info.gc().consumers, {})
    end)
end

g.test_gc_consumers_rollback = function(cg)
    cg.master:exec(create_and_fill_space)

    cg.master:exec(function()
        local fiber = require('fiber')
        local done = false
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}

        local fiber_f = function()
            box.begin()
            box.space._gc_consumers:replace{replica_uuid, box.info.vclock}
            while not done do
                fiber.sleep(0)
            end
            box.rollback()
        end
        local fib = fiber.create(fiber_f)
        fib:set_joinable(true)

        box.space._cluster:delete(10)
        done = true
        fib:join()
        t.assert_equals(box.info.gc().consumers, {})
    end)
end

-- Check if manipulations inside one transaction work correctly.
-- Since we cannot drop consumers of registered replicas, _cluster:delete
-- is used to drop consumers.
g.test_gc_consumers_in_transactions = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        local vclock = setmetatable({[1] = 4242}, { __serialize = 'map' })

        box.begin()
        box.space._cluster:insert{10, replica_uuid}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._cluster:delete(10)
        box.rollback()

        -- No consumers should be registered
        t.assert_equals(box.info.gc().consumers, {})

        box.begin()
        box.space._cluster:insert{10, replica_uuid}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._cluster:delete(10)
        box.commit()

        -- No consumers should be registered
        t.assert_equals(box.info.gc().consumers, {})

        box.space._cluster:insert{10, replica_uuid}

        box.begin()
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.rollback()

        -- No consumers should be registered
        t.assert_equals(box.info.gc().consumers, {})

        box.begin()
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.commit()

        -- One consumer should be registered
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(box.info.gc().consumers[1].vclock, vclock)
    end)
end

-- Check if manipulations inside one transaction together with space _cluster
-- work correctly.
g.test_gc_consumers_and_cluster_in_transactions = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        local vclock = box.info.vclock

        box.begin()
        box.space._cluster:insert{10, replica_uuid}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.rollback()
        t.assert_equals(box.info.gc().consumers, {})

        box.begin()
        box.space._cluster:insert{10, replica_uuid}
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.commit()
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(box.info.gc().consumers[1].vclock, vclock)

        -- Re-create replica for the next test case
        -- Consumer is dropped with replica
        box.space._cluster:delete(10)
        box.space._cluster:insert{10, replica_uuid}

        box.begin()
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._cluster:delete(10)
        box.space._gc_consumers:delete(replica_uuid)
        box.rollback()
        t.assert_equals(box.info.gc().consumers, {})

        box.begin()
        box.space._gc_consumers:replace{replica_uuid, vclock}
        box.space._cluster:delete(10)
        box.space._gc_consumers:delete(replica_uuid)
        box.commit()
        t.assert_equals(box.info.gc().consumers, {})
    end)
end

-- Check if concurrent manipulation over space _gc_consumers work correctly
g.test_gc_consumers_concurrency = function(cg)
    cg.master:exec(create_and_fill_space)

    cg.master:exec(function()
        local fiber = require('fiber')
        local done = false
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}

        local fiber_f = function()
            box.begin()
            local vclock = box.info.vclock
            vclock[1] = math.random(2000, 5000)
            box.space._gc_consumers:replace{replica_uuid, vclock}
            while not done do
                fiber.sleep(0)
            end
            -- Yield with small probability to shuffle order of finalization
            while math.random(3) == 1 do
                fiber.sleep(0)
            end
            if math.random(3) == 1 then
                box.rollback()
            else
                box.commit()
            end
        end

        -- Several loops to test both insert and replace
        for _ = 1, 3 do
            done = false
            local fibs = {}
            for _ = 1, 200 do
                local fib = fiber.create(fiber_f)
                fib:set_joinable(true)
                table.insert(fibs, fib)
            end

            done = true
            for _, fib in pairs(fibs) do
                fib:join()
            end
            t.assert_equals(#box.info.gc().consumers, 1)
            local persisted_vclock =
                box.space._gc_consumers:get(replica_uuid)[2]
            persisted_vclock[0] = nil
            t.assert_equals(box.info.gc().consumers[1].vclock, persisted_vclock)
        end
    end)
end

-- In Tarantool, we used to abort all in-progress transactions
-- on DLL, and now replace in any system space is considered as DDL.
-- What's about space _gc_consumers, we can have many replaces on it
-- when replication is enabled, so it would often abort user transactions
-- with no actual reason. So the test checks is space _gc_consumers behaves
-- in a special way and does NOT abort in-progress transactions.
g.test_gc_consumers_do_not_abort_txns = function(cg)
    cg.master:exec(function()
        local fiber = require('fiber')
        local done = false
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}

        local s = box.schema.space.create('s')
        s:create_index('pk')

        local fiber_f = function(i)
            box.begin()
            s:replace{i}
            while not done do
                fiber.sleep(0)
            end
            box.commit()
        end

        local fibs = {}
        for i = 1, 200 do
            local fib = fiber.create(fiber_f, i)
            fib:set_joinable(true)
            table.insert(fibs, fib)
        end

        box.space._gc_consumers:replace{replica_uuid, box.info.vclock}

        done = true
        for _, fib in pairs(fibs) do
            fib:join()
        end
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals(#s:select(), 200)
    end)
end

-- This test should be deleted when the option will be removed
g.test_wal_cleanup_delay_integration = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        for i = 2, 10 do
            local replica_uuid = uuid.str()
            box.space._cluster:insert{i, replica_uuid}
            box.space._gc_consumers:replace{replica_uuid, box.info.vclock}
        end
        -- Add replica without consumer to pause gc
        local replica_uuid = uuid.str()
        box.space._cluster:insert{11, replica_uuid}
    end)
    cg.master:restart{box_cfg = {wal_cleanup_delay = 3600}}
    cg.master:exec(function()
        -- GC should be paused for replica without consumer
        t.assert_equals(box.info.gc().is_paused, true)
        box.space._cluster:delete(11)
        -- The hanging replica was dropped - GC should be started
        t.assert_equals(box.info.gc().is_paused, false)
    end)
    cg.master:restart{box_cfg = {wal_cleanup_delay = 3600}}
    cg.master:exec(function()
        -- All replicas have GC consumers so it should be started
        t.assert_equals(box.info.gc().is_paused, false)
    end)
end
