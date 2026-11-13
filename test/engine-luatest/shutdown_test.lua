local server = require('luatest.server')
local cluster = require('luatest.replica_set')
local utils = require('luatest.utils')
local justrun = require('luatest.justrun')
local fio = require('fio')
local fiber = require('fiber')
local t = require('luatest')

local g = t.group('engine', {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_each(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

local function test_no_hang_on_shutdown(server)
    local channel = fiber.channel()
    fiber.create(function()
        server:stop()
        channel:put('finished')
    end)
    t.assert(channel:get(60) ~= nil)
end

-- Test we can interrupt loop on secondary index creation.
g.test_shutdown_secondary_index_build = function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server:exec(function(engine)
        local fiber = require('fiber')
        box.schema.create_space('test', {engine = engine})
        box.space.test:create_index('pk')
        fiber.set_slice(100)
        box.begin()
        for i=1,10000 do
            box.space.test:insert{i, i}
        end
        box.commit()
        box.error.injection.set('ERRINJ_BUILD_INDEX_TIMEOUT', 0.01)
        fiber.new(function()
            box.space.test:create_index('sk', {parts = {2}})
        end)
    end, {cg.params.engine})
    test_no_hang_on_shutdown(cg.server)
end

-- Luatest server currently does not allow to check process exit code.
local g_crash = t.group('crash', {
    {engine = 'memtx', memtx_use_mvcc_engine = true},
    {engine = 'memtx', memtx_use_mvcc_engine = false},
    {engine = 'vinyl'}
})

g_crash.before_each(function(cg)
    local id = ('%s-%s'):format('server', utils.generate_id())
    cg.workdir = fio.pathjoin(server.vardir, id)
    fio.mkdir(cg.workdir)
end)

-- Test shutdown when there is an active transaction.
g_crash.test_shutdown_with_active_txn = function(cg)
    local script_base = [[
        box.cfg{
            memtx_use_mvcc_engine = %s,
            log_level = 'error',
        }

        box.schema.space.create('test', {engine = '%s'})
        box.space.test:create_index('pk')
        box.begin()
        box.space.test:replace{5}
        box.space.test:get(10)
        box.space.test:select()
        os.exit()
    ]]
    local script = script_base:format(cg.params.memtx_use_mvcc_engine,
                                      cg.params.engine)

    local result = justrun.tarantool(cg.workdir, {}, {'-e', script},
                                     {nojson = true, quote_args = true,
                                      stderr = true})
    t.assert_covers(result, {exit_code = 0, stderr = ''})
end

local g_shutdown = t.group('shutdown_applier')

g_shutdown.before_each(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.cluster = cluster:new{}
    local box_cfg = {
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id),
        },
    }
    cg.master = cg.cluster:build_and_add_server({
        alias = 'master',
        box_cfg = box_cfg,
    })
    box_cfg.read_only = true
    cg.replica = cg.cluster:build_and_add_server({
        alias = 'replica',
        box_cfg = box_cfg,
    })
    cg.cluster:start()
end)

g_shutdown.after_each(function(cg)
    cg.cluster:drop()
end)

-- Test shutdown when a replica is applying a transaction whose WAL write is
-- still in flight (#10881). The applier commits it with SUBMIT, so no fiber
-- owns it; the WAL completion callback is what normally frees it. If the
-- process exits before the callback runs, the transaction stays in the global
-- txns list. Before commit 86e74a15e7db ("box: free formerly in-progress
-- txns on tarantool exit") nothing freed it at exit and LeakSanitizer
-- reported the txn region allocations; now box_storage_free() frees them.
--
-- The delay must outlast the shutdown from SIGTERM to wal_free(). Ten seconds
-- is far more than the round trips below; the stop is issued as soon as the
-- WAL write is queued, while the WAL thread is still sleeping.
g_shutdown.test_applier_txn_pending_at_shutdown = function(cg)
    cg.replica:exec(function()
        box.error.injection.set('ERRINJ_WAL_DELAY_DURATION', 10)
    end)
    local wal_cnt = cg.replica:exec(function()
        return box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
    end)
    local replica_id = cg.replica:get_instance_id()
    -- on_replace_dd_cluster() allocates from the txn region, the allocation
    -- the issue reports.
    cg.master:exec(function(id)
        box.space._cluster:delete{id}
    end, {replica_id})
    cg.replica:exec(function(cnt)
        t.helpers.retrying({timeout = 10}, function()
            local count =
                box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
            t.assert_gt(count, cnt)
        end)
    end, {wal_cnt})
    -- Luatest's stop() checks the server log for LeakSanitizer reports and
    -- fails the test if one appears.
    cg.replica:stop()
end
