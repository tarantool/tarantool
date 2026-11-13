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

-- Test shutdown when an asynchronous transaction that allocates from the txn
-- region is still in the WAL queue (gh-10881). The transaction is committed
-- with `wait='none'', so no fiber owns it; the WAL completion callback is what
-- normally frees it. This is the asynchronous counterpart of the active-txn
-- test above.
g_crash.test_shutdown_with_async_txn = function(cg)
    t.tarantool.skip_if_not_debug()
    -- The transaction is on the memtx `_cluster' space, so the vinyl variant
    -- would be identical to the memtx one.
    t.skip_if(cg.params.engine == 'vinyl',
              'the _cluster transaction is memtx')
    -- The instance takes id 1, so id 2 is free for a fake replica.
    local script_base = [[
        box.cfg{
            memtx_use_mvcc_engine = %s,
            log_level = 'error',
        }
        box.error.injection.set('ERRINJ_WAL_DELAY_DURATION', 1.0)
        box.begin()
        box.space._cluster:insert{2, require('uuid').str()}
        box.commit({wait = 'none'})
        os.exit()
    ]]
    local script = script_base:format(cg.params.memtx_use_mvcc_engine)

    local result = justrun.tarantool(cg.workdir, {}, {'-e', script},
                                     {nojson = true, quote_args = true,
                                      stderr = true})
    t.assert_covers(result, {exit_code = 0, stderr = ''})
end

local g_cluster = t.group('cluster')

g_cluster.before_each(function(cg)
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

g_cluster.after_each(function(cg)
    if cg.cluster ~= nil then
        cg.cluster:drop()
    end
end)

-- Test shutdown when a replica is applying a transaction whose WAL write is
-- still in flight (gh-10881). The applier commits it with `SUBMIT', so no fiber
-- owns it; the WAL completion callback is what normally frees it, and it never
-- runs after `wal_free()' stops the WAL thread.
--
-- If the process exits before the callback runs, the transaction stays in the
-- global `txns' list. Commit 86e74a15e7db ("box: free formerly in-progress txns
-- on tarantool exit") fixed this: `box_storage_free()' now frees the leftovers
-- at exit.
--
-- A replica-side `on_replace' trigger parks the WAL thread when the replicated
-- `_cluster' delete is applied: `ERRINJ_WAL_DELAY' holds the write until
-- shutdown starts, then `ERRINJ_WAL_DELAY_DURATION' keeps it parked long enough
-- for the shutdown to reach `wal_free()'. A fake row keeps the real replica and
-- its relay out of the path.
g_cluster.test_applier_txn_pending_at_shutdown = function(cg)
    t.tarantool.skip_if_not_debug()
    -- The master takes id 1 and the replica id 2, so 3 is free.
    local fake_id = 3
    cg.master:exec(function(id)
        box.space._cluster:insert{id, require('uuid').str()}
    end, {fake_id})
    local wal_cnt = cg.replica:exec(function(id)
        t.helpers.retrying({timeout = 10}, function()
            t.assert(box.space._cluster:get{id} ~= nil)
        end)
        box.space._cluster:on_replace(function(old, new)
            if old ~= nil and old[1] == id and new == nil then
                -- Park the WAL thread until the shutdown starts (if left set,
                -- it would block `wal_free()'s join forever), then make it wait
                -- one second so the shutdown reaches the join while the write
                -- is still parked; the exit waits that second out, so it stays
                -- short.
                box.error.injection.set('ERRINJ_WAL_DELAY', true)
                box.error.injection.set('ERRINJ_WAL_DELAY_DURATION', 1.0)
                box.ctl.on_shutdown(function()
                    box.error.injection.set('ERRINJ_WAL_DELAY', false)
                end)
            end
        end)
        return box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
    end, {fake_id})
    -- `on_replace_dd_cluster()' allocates from the txn region, the allocation
    -- the issue reports.
    cg.master:exec(function(id)
        box.space._cluster:delete{id}
    end, {fake_id})
    cg.replica:exec(function(cnt)
        t.helpers.retrying({timeout = 10}, function()
            local count =
                box.error.injection.get('ERRINJ_WAL_WRITE_COUNT')
            t.assert_gt(count, cnt)
        end)
    end, {wal_cnt})
    cg.replica:stop()
end
