--
-- Using space:before_replace to resolve replication conflicts.
--
env = require('test_run')
test_run = env.new()

SERVERS = { 'autobootstrap1', 'autobootstrap2', 'autobootstrap3' }

-- Deploy a cluster.
test_run:create_cluster(SERVERS)
test_run:wait_fullmesh(SERVERS)

-- Setup space:before_replace trigger on all replicas.
-- The trigger favors tuples with a greater value.
test_run:cmd("setopt delimiter ';'")
test_run:cmd("switch autobootstrap1");
_ = box.space.test:before_replace(function(old, new)
    if old ~= nil and new ~= nil then
        return new[2] > old[2] and new or old
    end
end);
test_run:cmd("switch autobootstrap2");
_ = box.space.test:before_replace(function(old, new)
    if old ~= nil and new ~= nil then
        return new[2] > old[2] and new or old
    end
end);
test_run:cmd("switch autobootstrap3");
_ = box.space.test:before_replace(function(old, new)
    if old ~= nil and new ~= nil then
        return new[2] > old[2] and new or old
    end
end);
test_run:cmd("setopt delimiter ''");

-- Stall replication and generate incompatible data
-- on the replicas.
test_run:cmd("switch autobootstrap1")
box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0.01)
for i = 1, 10 do box.space.test:replace{i, i % 3 == 1 and i * 10 or i} end
test_run:cmd("switch autobootstrap2")
box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0.01)
for i = 1, 10 do box.space.test:replace{i, i % 3 == 2 and i * 10 or i} end
test_run:cmd("switch autobootstrap3")
box.error.injection.set('ERRINJ_RELAY_TIMEOUT', 0.01)
for i = 1, 10 do box.space.test:replace{i, i % 3 == 0 and i * 10 or i} end

-- Synchronize.
test_run:cmd("switch default")
vclock = test_run:get_cluster_vclock(SERVERS)
vclock2 = test_run:wait_cluster_vclock(SERVERS, vclock)

-- Check that all replicas converged to the same data
-- and the state persists after restart.
test_run:cmd("switch autobootstrap1")
box.space.test:select()
test_run:cmd('restart server autobootstrap1')
box.space.test:select()
test_run:cmd("switch autobootstrap2")
box.space.test:select()
test_run:cmd('restart server autobootstrap2')
box.space.test:select()
test_run:cmd("switch autobootstrap3")
box.space.test:select()
test_run:cmd('restart server autobootstrap3')
box.space.test:select()


-- Cleanup.
test_run:cmd("switch default")
test_run:drop_cluster(SERVERS)
