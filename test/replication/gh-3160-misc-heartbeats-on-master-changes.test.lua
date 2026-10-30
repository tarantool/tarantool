test_run = require('test_run').new()

-- gh-3160 - Send heartbeats if there are changes from a remote master only
SERVERS = { 'autobootstrap1', 'autobootstrap2', 'autobootstrap3' }

-- Deploy a cluster.
test_run:create_cluster(SERVERS, "replication", {args="0.03"})
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch autobootstrap3")
test_run = require('test_run').new()
_ = box.schema.space.create('test_timeout'):create_index('pk')
test_run:cmd("setopt delimiter ';'")

local function replica(id)
    return box.info.replication[id].upstream
end

function wait_not_follow(id_a, id_b)
    return test_run:wait_cond(function()
        return replica(id_a).status ~= 'follow' or
               replica(id_b).status ~= 'follow'
    end, box.cfg.replication_timeout / 5)
end;

function test_timeout()
    local id_a = box.info.id % 3 + 1
    local id_b = id_a % 3 + 1
    local follows = test_run:wait_upstream(id_a, {status = 'follow'})
    follows = follows and test_run:wait_upstream(id_b, {status = 'follow'})
    if not follows then error('replicas are not in the follow status') end
    for i = 0, 99 do
        box.space.test_timeout:replace({1})
        if wait_not_follow(id_a, id_b) then
            require('log').error(box.info.replication)
            return false
        end
    end
    return true
end;
test_run:cmd("setopt delimiter ''");
test_timeout()

test_run:cmd("switch default")
test_run:drop_cluster(SERVERS)
