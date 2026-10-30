--
-- gh-6018: in an auto-election cluster nodes with voter state could be selected
-- as bootstrap leaders. They should not, because a voter can't be ever writable
-- and it can neither boot itself nor register other nodes.
--
-- Similar situation was with the manual election. All instances might have
-- manual election mode. Such a cluster wouldn't be able to boot if their
-- bootstrap master wouldn't become an elected leader automatically at least
-- once.
--
test_run = require('test_run').new()

function boot_with_master_election_mode(mode)                                   \
        test_run:cmd('create server master with '..                             \
                     'script="replication/gh-6018-master.lua"')                 \
        test_run:cmd('start server master with wait=False, args="'..mode..'"')  \
        test_run:cmd('create server replica with '..                            \
                     'script="replication/gh-6018-replica.lua"')                \
        test_run:cmd('start server replica')                                    \
end

function stop_cluster()                                                         \
    test_run:cmd('stop server replica')                                         \
    test_run:cmd('stop server master')                                          \
    test_run:cmd('delete server replica')                                       \
    test_run:cmd('delete server master')                                        \
end

--
-- Candidate leader.
--
boot_with_master_election_mode('candidate')

test_run:switch('master')
test_run:wait_cond(function() return not box.info.ro end)
assert(box.info.election.state == 'leader')

test_run:switch('replica')
assert(box.info.ro)
assert(box.info.election.state == 'follower')

test_run:switch('default')
stop_cluster()

--
-- Manual leader.
--
boot_with_master_election_mode('manual')

test_run:switch('master')
test_run:wait_cond(function() return not box.info.ro end)
assert(box.info.election.state == 'leader')

test_run:switch('replica')
assert(box.info.ro)
assert(box.info.election.state == 'follower')

test_run:switch('default')
stop_cluster()
