local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fio = require('fio')

local g = t.group('gh-7512')

-- gh-7512: Raft followers should notice leader hang and arrange new elections.
g.before_all(function(cg)
    cg.cluster = cluster:new{}
    local box_cfg = {
        election_mode = 'candidate',
        replication_timeout = 0.1,
        replication_synchro_quorum = 1,
        replication = {
            server.build_listen_uri('node1'),
            server.build_listen_uri('node2'),
        },
    }
    cg.node1 = cg.cluster:build_and_add_server{
        alias = 'node1',
        box_cfg = box_cfg,
    }
    box_cfg.election_mode='voter'
    cg.node2  = cg.cluster:build_and_add_server{
        alias = 'node2',
        box_cfg = box_cfg,
    }
    cg.cluster:start()

    cg.unique_filename = server.build_listen_uri('unique_filename')
    fio.unlink(cg.unique_filename)
end)

g.after_all(function(cg)
    cg.cluster:drop()
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

g.test_leader_hang_notice = function(cg)
    cg.node2:exec(function()
        box.cfg{election_mode='candidate'}
    end)
    local term = cg.node2:get_election_term()
    cg.node1:exec(block, {cg.unique_filename})
    t.helpers.retrying({}, server.exec, cg.node2, function(term)
        t.assert_equals(box.info.replication[1].upstream.status, 'disconnected',
                        'Hang is noticed')
        t.assert_equals(box.info.election.term, term + 1, 'Term is bumped')
        t.assert_equals(box.info.election.state, 'leader',
                        'New leader is nominated')
    end, {term})
    -- Unblock node1.
    fio.open(cg.unique_filename, {'O_CREAT'}):close()

end
