local t = require('luatest')
local replica_set = require('luatest.replica_set')
local server = require('luatest.server')

local fio = require('fio')

local g = t.group('gh_7515_notice_leader_hang_during_sync')

g.before_each(function(cg)
    cg.replica_set = replica_set:new{}
    local box_cfg = {
        replication_timeout = 0.1,
        replication = {
            server.build_listen_uri('server_gh7515_1'),
            server.build_listen_uri('server_gh7515_2'),
            server.build_listen_uri('server_gh7515_3'),
        },
    }
    for i = 1, 3 do
        local alias = 'server_gh7515_' .. i
        if i == 1 then
            box_cfg.election_mode = 'candidate'
        else
            box_cfg.election_mode = 'voter'
        end
        cg['server' .. i] = cg.replica_set:build_and_add_server{
            alias = alias,
            box_cfg = box_cfg,
        }
    end
    cg.replica_set:start()
    cg.unique_filename = server.build_listen_uri('unique_filename')
    fio.unlink(cg.unique_filename)
end)

g.after_each(function(cg)
    cg.replica_set:drop()
    fio.unlink(cg.unique_filename)
end)

local function block(filename)
    local fiber = require('fiber')
    fiber.new(function(filename)
        -- Can't use fio, because it yields.
        local cmd = 'sleep 0.1 && ls ' .. filename .. ' > /dev/null 2>&1'
        while os.execute(cmd) ~= 0 do end
    end, filename)
end

g.test_notice_leader_hang_during_sync = function(cg)
    t.tarantool.skip_if_not_debug()

    cg.server1:wait_for_election_leader()

    cg.server2:stop()
    -- Generate some data so that server2 starts syncing with the leader after
    -- restart. Generate transactions, to make sure heartbeats do not interfere
    -- with transactions, and make relay sleep after each sent row to make sure
    -- it takes server2 long enough to sync.
    local old_term = cg.server1:exec(function()
        box.schema.space.create('test')
        box.space.test:create_index('pk')
        for i = 1, 5 do
            box.begin()
            for j = 1, 5 do
                box.space.test:replace{j, i}
            end
            box.commit()
        end
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT',
                                box.cfg.replication_timeout)
        return box.info.election.term
    end)
    cg.server2.box_cfg.replication_sync_timeout = 0
    table.remove(cg.server2.box_cfg.replication, 3)
    cg.server2:start()
    t.helpers.retrying({}, cg.server2.exec, cg.server2, function()
        t.assert_equals(box.info.replication[1].upstream.status, 'sync',
                        'Server is syncing with the leader')
    end)

    cg.server1:exec(block, {cg.unique_filename})

    t.helpers.retrying({}, cg.server2.exec, cg.server2, function(term)
        local timeout = box.cfg.replication_timeout
        t.assert_le(box.info.replication[1].upstream.idle, timeout,
                    'A hung leader sends data')
        t.assert_ge(box.info.election.leader_idle, 4 * timeout,
                    'A hung leader doesn\'t send heartbeats during sync')
        t.assert_ge(box.info.election.term, term, 'Leader hang is noticed')
    end, {old_term})

    -- Unblock server1.
    fio.open(cg.unique_filename, {'O_CREAT'}):close()
    cg.server1:exec(function()
        box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0)
    end)
end
