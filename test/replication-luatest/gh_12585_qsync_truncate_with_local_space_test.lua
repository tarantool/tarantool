local t = require('luatest')
local server = require('luatest.server')
local replica_set = require('luatest.replica_set')

local g = t.group('qsync-truncate-with-local-space')

g.before_all(function(g)
    g.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
        replication_synchro_quorum = 3,
        replication_synchro_timeout = 600,
        election_fencing_mode = 'off',
        election_mode = 'manual',
        replication = {
            server.build_listen_uri('master', g.replica_set.id),
            server.build_listen_uri('replica', g.replica_set.id),
        },
    }
    g.master = g.replica_set:build_and_add_server{
        alias = 'master', box_cfg = box_cfg}
    g.replica = g.replica_set:build_and_add_server{
        alias = 'replica', box_cfg = box_cfg}
    g.replica_set:start()
    g.replica_set:wait_for_fullmesh()
    g.master:exec(function()
        t.assert(pcall(box.ctl.promote))
        box.ctl.wait_rw()
        box.schema.space.create('sync', {is_sync = true}):create_index('pk')
        box.schema.space.create('test', {is_local = true}):create_index('pk')
        box.space._truncate:alter{is_sync = true}
    end)
    g.replica:wait_for_vclock_of(g.master)
end)

g.after_all(function(g)
    g.replica_set:drop()
end)

g.before_each(function(g)
    -- After restart manual promote is required.
    g.replica_set:wait_for_fullmesh()
    g.master:exec(function()
        t.assert(pcall(box.ctl.promote))
        box.ctl.wait_rw()
    end)
    -- Make local space non-empty.
    for _, s in ipairs{'replica', 'master'} do
        g[s]:exec(function()
            box.space.test:replace{1}
        end)
    end
end)

local function test_server_can_truncate_template(server)
    server:exec(function()
        box.space.test:truncate()
    end)
    server:restart()
    server:exec(function()
        t.assert_equals(box.space.test:count(), 0)
    end)
end

g.test_master_can_truncate = function(g)
    test_server_can_truncate_template(g.master)
end

g.test_replica_can_truncate = function(g)
    test_server_can_truncate_template(g.replica)
end

local function wait_synchro_queue_len(len)
    t.helpers.retrying({timeout = 10}, function()
        t.assert_equals(box.info.synchro.queue.len, len)
    end)
end

local function test_server_truncate_with_non_empty_limbo(server)
    g.master:exec(function()
        local fiber = require('fiber')
        local count = box.space._cluster:count()
        t.assert_gt(box.cfg.replication_synchro_quorum, count)
        rawset(_G, 'sync_replace_f', fiber.create(function()
            fiber.self():set_joinable(true)
            box.space.sync:replace{1}
        end))
    end)
    server:exec(wait_synchro_queue_len, {1})
    server:exec(function()
        require('fiber').create(function()
            box.space.test:truncate()
        end)
    end)
    server:exec(wait_synchro_queue_len, {2})
    g.master:update_box_cfg{replication_synchro_quorum = 2}
    g.master:exec(function()
        _G.sync_replace_f:join()
        _G.sync_replace_f = nil
    end)
    server:exec(wait_synchro_queue_len, {0})
    server:exec(function()
        t.assert_equals(box.space.test:count(), 0)
    end)
    g.master:update_box_cfg{replication_synchro_quorum = 3}
end

g.test_master_truncate_with_non_empty_limbo = function(g)
    test_server_truncate_with_non_empty_limbo(g.master)
end

g.test_replica_truncate_with_non_empty_limbo = function(g)
    test_server_truncate_with_non_empty_limbo(g.replica)
end
