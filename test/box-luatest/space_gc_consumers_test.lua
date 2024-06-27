local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')
local urilib = require('uri')

local g = t.group('Local upgrade')

g.before_each(function(cg)
    cg.server = server:new({
        datadir = 'test/box-luatest/upgrade/3.0.0',
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
end

g = t.group('Cluster upgrade')

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
    }
    cg.servers = {}
    for i = 1, 3 do
        local datadir =
            string.format('test/box-luatest/upgrade/3.0.0-cluster/server%d', i)
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

    leader:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
    end)

    -- Check if all gc consumers are persisted on all replicas
    for _, s in pairs(cg.servers) do
        s:wait_for_vclock_of(leader)
    end
    for _, s in pairs(cg.servers) do
        s:exec(function(server_num)
            -- No gc consumer for self
            local consumer_num = server_num - 1
            t.assert_equals(#box.space._gc_consumers:select(), consumer_num)
        end, {#cg.servers})
    end
end

g = t.group('Space _gc_consumers')

local function create_and_fill_space()
    local s = box.schema.create_space('test')
    s:create_index('pk')
    for i = 1, 2 do
        box.begin()
        for j = 1, 50 do
            box.space.test:replace{i, j}
        end
        box.commit()
        box.snapshot()
    end
end

local function load_space()
    for i = 1, 2 do
        box.begin()
        for j = 1, 50 do
            box.space.test:replace{i, j}
        end
        box.commit()
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
    if cg.replica ~= nil then
        cg.replica:drop()
        cg.replica = nil
    end
end)

g.test_gc_consumers_basic_test = function(cg)
    cg.master:exec(create_and_fill_space)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local vclock = box.info.vclock
        vclock[0] = nil
        setmetatable(vclock, {__serialize = 'map'})
        local opts = {type = 'replica'}
        box.space._gc_consumers:replace{replica_uuid, vclock, opts}
    end)
    local saved_vclock = cg.master:get_vclock()

    -- Restart master to load consumers from disk
    cg.master:restart()

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

g.test_gc_consumers_object_name = function(cg)
    local consumers = cg.master:exec(function()
        local luuid = require('uuid')
        local map = setmetatable({}, {__serialize = 'map'})
        local consumers = {}

        local uuid = luuid.str()
        box.space._gc_consumers:replace{uuid, map, {type = 'replica'}}
        table.insert(consumers, 'replica ' .. uuid)

        uuid = luuid.str()
        box.space._gc_consumers:replace{uuid, map, {object = 'invalid'}}

        uuid = luuid.str()
        box.space._gc_consumers:replace{uuid, map, map}

        return consumers
    end)
    cg.master:restart()
    cg.master:exec(function(expected_consumers)
        t.assert_equals(#box.info.gc().consumers, 1)
        t.assert_equals({box.info.gc().consumers[1].name}, expected_consumers)
    end, {consumers})
end

-- The test that checks that invalid consumers do not break Tarantool. Also
-- the test do some simple checks that consumers' issues are logged.
g.test_gc_consumers_invalid_consumer = function(cg)
    cg.master:exec(function()
        local luuid = require('uuid')
        local map = setmetatable({}, {__serialize = 'map'})

        local uuid = luuid.str()
        box.space._gc_consumers:replace{uuid, map,
            {invalid = true, type = 'replica'}}

        local uuid = luuid.str()
        box.space._gc_consumers:replace{uuid, map, {type = 'unknown'}}

        uuid = luuid.str()
        local vclock = {key = 10}
        box.space._gc_consumers:replace{uuid, vclock, map}

        uuid = luuid.str()
        vclock = {[33] = 1}
        box.space._gc_consumers:replace{uuid, vclock, map}

        box.space._gc_consumers:replace{'abc', map, map}
    end)
    cg.master:restart()
    cg.master:exec(function()
        t.assert_equals(box.info.gc().consumers, {})
    end)
    t.assert(cg.master:grep_log('Error while recovering GC consumer'))
    t.assert(cg.master:grep_log('Error while recovering GC consumer abc'))
    t.assert(cg.master:grep_log('has invalid type'))
    t.assert(cg.master:grep_log('Invalid vclock'))
end

-- Test that outdated consumer becomes active after restart.
-- Outdated consumer is a consumer that fell behind `gc.vclock`,
-- or, in other words, vclock of the oldest xrow available.
-- It is an important case because all persistent consumers
-- are updated asynchronously, so many of active consumers
-- may become outdated after restart since they are a little
-- behind in-memory ones, that actually pin xlogs.
g.test_gc_consumers_outdated_consumer = function(cg)
    cg.master:exec(create_and_fill_space)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local vclock = box.info.vclock
        vclock[1] = 1
        local opts = {type = 'replica'}
        box.space._gc_consumers:replace{replica_uuid, vclock, opts}
        t.assert_equals(box.info.gc().consumers, {})
    end)

    -- Restart master to load consumers from disk
    cg.master:restart()

    cg.master:exec(load_space)
    cg.master:exec(function()
        -- Check if consumer hasn't changed
        local consumers = box.space._gc_consumers:select{}
        t.assert_equals(#consumers, 1)
        t.assert_equals(consumers[1].vclock[1], 1)

        t.assert_not_equals(box.info.gc().consumers, {})
    end)
end

-- Check if consumer is deleted on replica delete
g.test_gc_consumers_on_replica_delete = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local opts = setmetatable({}, {__serialize = 'map'})
        box.space._gc_consumers:insert{replica_uuid, box.info.vclock, opts}
        box.space._cluster:delete(10)
        t.assert_equals(box.space._gc_consumers:select{}, {})
    end)
    -- Restart to check if consumer is not deleted again on recovery
    cg.master:restart()

    -- Join a replica to check if consumer is not deleted on recovery
    local uri = urilib.parse(cg.master.net_box_uri)
    cg.replica = server:new{box_cfg = {replication = uri.unix}}
    cg.replica:start()
    cg.replica:wait_for_vclock_of(cg.master)

    -- Restart to check if WAL is OK (if consumer is deleted on recovery,
    -- WAL will be broken).
    cg.replica:restart()
end

-- Check if consumer is deleted on replica uuid update
g.test_gc_consumers_on_replica_uuid_update = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        local new_replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid, 'name'}
        local opts = setmetatable({}, {__serialize = 'map'})
        box.space._gc_consumers:replace{replica_uuid, box.info.vclock, opts}

        box.space._cluster:replace{10, new_replica_uuid, 'name'}
        -- Consumer of old replica must be deleted
        t.assert_equals(box.space._gc_consumers:select(), {})
    end)
    -- Restart to check if consumer is not deleted again on recovery
    cg.master:restart()
end

g.test_gc_consumers_on_replica_name_update = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        local replica_uuid = uuid.str()
        box.space._cluster:insert{10, replica_uuid}
        local opts = setmetatable({}, {__serialize = 'map'})
        local consumer =
            box.space._gc_consumers:replace{replica_uuid, box.info.vclock, opts}

        box.space._cluster:replace{10, replica_uuid, 'name'}
        -- Consumer should stay intact after populating replica with name.
        t.assert_equals(box.space._gc_consumers:select(), {consumer})
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
        local opts = setmetatable({}, {__serialize = 'map'})
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

        box.space._gc_consumers:replace{replica_uuid, box.info.vclock, opts}

        done = true
        for _, fib in pairs(fibs) do
            fib:join()
        end
        t.assert_equals(#s:select(), 200)
        t.assert_equals(#box.space._gc_consumers:select{}, 1)
    end)
end

-- This test should be deleted when the option will be removed
g.test_wal_cleanup_delay_integration = function(cg)
    cg.master:exec(function()
        local uuid = require('uuid')
        for i = 2, 10 do
            local replica_uuid = uuid.str()
            box.space._cluster:insert{i, replica_uuid}
            local opts = {type = 'replica'}
            box.space._gc_consumers:replace{replica_uuid, box.info.vclock, opts}
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
