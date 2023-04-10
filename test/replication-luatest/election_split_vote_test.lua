local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')

local wait_timeout = 120

--
-- gh-5285: split vote is when in the current term there can't be winner of the
-- leader role. Number of unused votes is not enough for anyone to get the
-- quorum. It can be detected to speed up the term bump.
--
local g = t.group('split-vote')

g.before_each(function()
    g.cluster = cluster:new({})
    local node1_uri = server.build_listen_uri('node1', g.cluster.id)
    local node2_uri = server.build_listen_uri('node2', g.cluster.id)
    local replication = {node1_uri, node2_uri}
    local box_cfg = {
        listen = node1_uri,
        replication = replication,
        -- To speed up new term when try to elect a first leader.
        replication_timeout = 0.1,
        replication_synchro_quorum = 2,
        election_timeout = 1000000,
    }
    g.node1 = g.cluster:build_server({alias = 'node1', box_cfg = box_cfg})

    box_cfg.listen = node2_uri
    g.node2 = g.cluster:build_server({alias = 'node2', box_cfg = box_cfg})

    g.cluster:add_server(g.node1)
    g.cluster:add_server(g.node2)
    g.cluster:start()
end)

g.after_each(function()
    g.cluster:drop()
end)

g.test_split_vote = function(g)
    -- Stop the replication so as the nodes can't request votes from each other.
    local node1_repl = g.node1:exec(function()
        local repl = box.cfg.replication
        box.cfg{replication = {}}
        return repl
    end)
    local node2_repl = g.node2:exec(function()
        local repl = box.cfg.replication
        box.cfg{replication = {}}
        return repl
    end)

    -- Both vote for self but don't see the split-vote yet.
    -- Have to promote the nodes to make them start elections when none of them
    -- sees a quorum of peers.
    g.node1:exec(function()
        box.cfg{election_mode = 'candidate'}
        require('fiber').new(box.ctl.promote)
    end)
    g.node2:exec(function()
        box.cfg{election_mode = 'candidate'}
        require('fiber').new(box.ctl.promote)
    end)

    -- Wait for the votes to actually happen.
    t.helpers.retrying({timeout = wait_timeout}, function()
        local func = function()
            t.assert_equals(box.info.election.vote, box.info.id)
        end
        g.node1:exec(func)
        g.node2:exec(func)
    end)

    -- Now let the nodes notice the split vote.
    g.node1:exec(function(repl)
        box.cfg{replication = repl}
    end, {node1_repl})
    g.node2:exec(function(repl)
        box.cfg{replication = repl}
    end, {node2_repl})

    t.helpers.retrying({timeout = wait_timeout}, function()
        local msg = 'split vote is discovered'
        t.assert(g.node1:grep_log(msg) or g.node2:grep_log(msg))
    end)

    -- Ensure a leader is eventually elected. Nothing is broken for good.
    g.node1:exec(function()
        box.cfg{election_timeout = 1}
    end)
    g.node2:exec(function()
        box.cfg{election_timeout = 1}
    end)
    g.node1:wait_until_election_leader_found()
    g.node2:wait_until_election_leader_found()
end
