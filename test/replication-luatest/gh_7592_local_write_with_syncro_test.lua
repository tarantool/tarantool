local t = require('luatest')
local server = require('luatest.server')
local cluster = require('luatest.replica_set')
local fiber = require('fiber')

local g = t.group('gh-7592')

g.before_each(function(cg)
    cg.cluster = cluster:new{}
    local box_cfg = {
        replication_synchro_timeout = 1000,
        replication_synchro_quorum = 3,
        replication_timeout = 0.1,
        election_mode = 'off',
        replication = {
            server.build_listen_uri('master', cg.cluster.id),
            server.build_listen_uri('replica', cg.cluster.id),
        },
    }
    cg.master = cg.cluster:build_and_add_server{
        alias = 'master',
        box_cfg = box_cfg,
    }
    cg.replica = cg.cluster:build_and_add_server{
        alias = 'replica',
        box_cfg = box_cfg,
    }
    cg.cluster:start()
    cg.master:exec(function()
        box.ctl.promote()
        box.schema.space.create('sync', {is_sync = true})
        box.space.sync:create_index('pk')
        box.schema.space.create('loc', {is_local = true})
        box.space.loc:create_index('pk')
    end)
    cg.replica:wait_for_vclock_of(cg.master)
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

local function wait_synchro_queue_has_entries(server)
    t.helpers.retrying({}, function()
        local len = server:exec(function()
            return box.info.synchro.queue.len
        end)
        t.assert(len > 0, 'Synchro queue is occupied')
    end)
end

g.test_local_space_write_on_replica = function(cg)
    -- Test that replica can write to local spaces when synchro queue is empty:
    cg.replica:exec(function()
        t.assert(box.info.synchro.queue.owner ~= 0, 'Queue is claimed')
        t.assert(box.info.synchro.queue.owner ~= box.info.id,
                 'Queue is foreign')
        t.assert(box.info.synchro.queue.len == 0, 'Queue is empty')
        local ok, _ = pcall(box.space.loc.insert, box.space.loc, {1})
        t.assert(ok, 'Local space is writeable')
    end)

    -- Test that replica can write to local spaces when synchro queue is filled:
    local f = fiber.new(function()
        cg.master:exec(function()
            box.space.sync:insert{1}
        end)
    end)
    f:set_joinable(true)
    wait_synchro_queue_has_entries(cg.replica)
    local fid = cg.replica:exec(function()
        t.assert(box.info.synchro.queue.len == 1, 'Queue is filled')
        t.assert(box.info.synchro.queue.owner ~= box.info.id,
                 'Queue is foreign')
        local r_f = require('fiber').create(function()
            box.space.loc:insert{2}
        end)
        r_f:set_joinable(true)
        t.assert(box.info.synchro.queue.len == 2,
                 'Local write is placed in the queue')
        return r_f:id()
    end)
    cg.master:exec(function() box.cfg{replication_synchro_quorum = 2} end)
    f:join()
    cg.replica:exec(function(fid)
        require('fiber').find(fid):join()
        t.assert(box.info.synchro.queue.len == 0, 'Queue is emptied')
        t.assert_equals(box.space.loc:get{2}, {2}, 'Data is written')
    end, {fid})
end
