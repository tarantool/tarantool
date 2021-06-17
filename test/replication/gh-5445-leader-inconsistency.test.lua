test_run = require("test_run").new()

is_leader_cmd = "return box.info.election.state == 'leader'"

-- Auxiliary.
test_run:cmd('setopt delimiter ";"')
function name(id)
    return 'election_replica'..id
end;

function get_leader(nrs)
    local leader_nr = 0
    test_run:wait_cond(function()
        for nr, do_check in pairs(nrs) do
            if do_check then
                local is_leader = test_run:eval(name(nr),
                                                is_leader_cmd)[1]
                if is_leader then
                    leader_nr = nr
                    return true
                end
            end
        end
        return false
    end)
    assert(leader_nr ~= 0)
    return leader_nr
end;

test_run:cmd('setopt delimiter ""');

--
-- gh-5445: make sure rolled back rows do not reappear once old leader returns
-- to cluster.
--
SERVERS = {'election_replica1', 'election_replica2' ,'election_replica3'}
test_run:create_cluster(SERVERS, "replication", {args='2 0.4'})
test_run:wait_fullmesh(SERVERS)

-- Any of the three instances may bootstrap the cluster and become leader.
is_possible_leader = {true, true, true}
leader_nr = get_leader(is_possible_leader)
leader = name(leader_nr)
next_leader_nr = ((leader_nr - 1) % 3 + 1) % 3 + 1 -- {1, 2, 3} -> {2, 3, 1}
next_leader = name(next_leader_nr)
other_nr = ((leader_nr - 1) % 3 + 2) % 3 + 1 -- {1, 2, 3} -> {3, 1, 2}
other = name(other_nr)

test_run:switch(other)
box.cfg{election_mode='voter'}
test_run:switch('default')

test_run:switch(next_leader)
box.cfg{election_mode='voter'}
test_run:switch('default')

test_run:switch(leader)
box.ctl.wait_rw()
_ = box.schema.space.create('test', {is_sync=true})
_ = box.space.test:create_index('pk')
box.space.test:insert{1}

-- Simulate a situation when the instance which will become the next leader
-- doesn't know of unconfirmed rows. It should roll them back anyways and do not
-- accept them once they actually appear from the old leader.
-- So, stop the instance which'll be the next leader.
test_run:switch('default')
test_run:cmd('stop server '..next_leader)
test_run:switch(leader)
-- Insert some unconfirmed data.
box.cfg{replication_synchro_quorum=3, replication_synchro_timeout=1000}
fib = require('fiber').create(box.space.test.insert, box.space.test, {2})
fib:status()

-- 'other', 'leader', 'next_leader' are defined on 'default' node, hence the
-- double switches.
test_run:switch('default')
test_run:switch(other)
-- Wait until the rows are replicated to the other instance.
test_run:wait_cond(function() return box.space.test:get{2} ~= nil end)
-- Old leader is gone.
test_run:switch('default')
test_run:cmd('stop server '..leader)
is_possible_leader[leader_nr] = false
-- And other node as well.
test_run:cmd('stop server '..other)
is_possible_leader[other_nr] = false

-- Emulate a situation when next_leader wins the elections. It can't do that in
-- this configuration, obviously, because it's behind the 'other' node, so set
-- quorum to 1 and imagine there are 2 more servers which would vote for
-- next_leader.
-- Also, make the instance ignore synchronization with other replicas.
-- Otherwise it would stall for replication_sync_timeout. This is due to the
-- nature of the test and may be ignored (we restart the instance to simulate
-- a situation when some rows from the old leader were not received).
test_run:cmd('start server '..next_leader..' with args="1 0.4 candidate 1"')
assert(get_leader(is_possible_leader) == next_leader_nr)
test_run:cmd('start server '..other..' with args="1 0.4 voter 2"')
is_possible_leader[other_nr] = true
test_run:switch(other)
-- New leader didn't know about the unconfirmed rows but still rolled them back.
test_run:wait_cond(function() return box.space.test:get{2} == nil end)

test_run:switch('default')
test_run:switch(next_leader)
-- No signs of the unconfirmed transaction.
box.space.test:select{} -- 1

test_run:switch('default')
-- Old leader returns and old unconfirmed rows from it must be ignored.
-- Note, it wins the elections fairly.
test_run:cmd('start server '..leader..' with args="3 0.4 voter"')
test_run:wait_lsn(leader, next_leader)
test_run:switch(leader)
test_run:wait_cond(function() return box.space.test:get{2} == nil end)
box.cfg{election_mode='candidate'}

test_run:switch('default')
test_run:switch(next_leader)
-- Resign to make old leader win the elections.
box.cfg{election_mode='voter'}

test_run:switch('default')
is_possible_leader[leader_nr] = true
assert(get_leader(is_possible_leader) == leader_nr)

test_run:switch(next_leader)
test_run:wait_upstream(1, {status='follow'})
box.space.test:select{} -- 1

-- Cleanup.
test_run:switch('default')
test_run:drop_cluster(SERVERS)
