env = require('test_run')
vclock_diff = require('fast_replica').vclock_diff
test_run = env.new()


SERVERS = { 'autobootstrap1', 'autobootstrap2', 'autobootstrap3' }

--
-- Start servers
--
test_run:create_cluster(SERVERS, "replication", {args="0.1"})

--
-- Wait for full mesh
--
test_run:wait_fullmesh(SERVERS)

--
-- Check vclock
--
vclock1 = test_run:get_vclock('autobootstrap1')
vclock_diff(vclock1, test_run:get_vclock('autobootstrap2'))
vclock_diff(vclock1, test_run:get_vclock('autobootstrap3'))

--
-- Insert rows on each server
--
_ = test_run:cmd("switch autobootstrap1")
_ = box.space.test:insert({box.info.id})
_ = test_run:cmd("switch autobootstrap2")
_ = box.space.test:insert({box.info.id})
_ = test_run:cmd("switch autobootstrap3")
_ = box.space.test:insert({box.info.id})
_ = test_run:cmd("switch default")

--
-- Synchronize
--

vclock = test_run:get_cluster_vclock(SERVERS)
vclock2 = test_run:wait_cluster_vclock(SERVERS, vclock)
vclock_diff(vclock1, vclock2)

--
-- Check result
--
_ = test_run:cmd("switch autobootstrap1")
box.space.test:select{}
_ = test_run:cmd("switch autobootstrap2")
box.space.test:select{}
_ = test_run:cmd("switch autobootstrap3")
box.space.test:select{}
_ = test_run:cmd("switch default")


_ = test_run:cmd("switch autobootstrap1")
u1 = box.schema.user.create('test_u')
box.schema.user.grant('test_u', 'create', 'space')
box.schema.user.grant('test_u', 'read,write', 'space', '_space')
box.schema.user.grant('test_u', 'write', 'space', '_schema')
box.schema.user.grant('test_u', 'write', 'space', '_index')
box.schema.user.grant('test_u', 'read', 'space', '_space_sequence')
box.session.su('test_u')
_ = box.schema.space.create('test_u'):create_index('pk')
box.session.su('admin')
_ = box.space.test_u:replace({1, 2, 3, 4})
box.space.test_u:select{}

box.schema.user.revoke('test_u', 'read', 'space', '_space_sequence')
box.schema.user.revoke('test_u', 'write', 'space', '_index')
box.schema.user.revoke('test_u', 'write', 'space', '_schema')
box.schema.user.revoke('test_u', 'read,write', 'space', '_space')
box.schema.user.revoke('test_u', 'create', 'space')

-- Synchronize
vclock = test_run:get_vclock('autobootstrap1')
vclock[0] = nil
_ = test_run:wait_vclock("autobootstrap2", vclock)
_ = test_run:wait_vclock("autobootstrap3", vclock)

_ = test_run:cmd("switch autobootstrap2")
box.space.test_u:select{}
_ = test_run:cmd("switch autobootstrap3")
box.space.test_u:select{}

--
-- Rebootstrap one node and check that others follow.
--
_ = test_run:cmd("switch autobootstrap1")
_ = test_run:cmd("restart server autobootstrap1 with cleanup=1, args ='0.1'")

_ = box.space.test_u:replace({5, 6, 7, 8})
box.space.test_u:select{}

_ = test_run:cmd("switch default")
test_run:wait_fullmesh(SERVERS)

vclock = test_run:get_vclock("autobootstrap1")
vclock[0] = nil
_ = test_run:wait_vclock("autobootstrap2", vclock)
_ = test_run:wait_vclock("autobootstrap3", vclock)

_ = test_run:cmd("switch autobootstrap2")
box.space.test_u:select{}
_ = test_run:cmd("switch autobootstrap3")
box.space.test_u:select{}

_ = test_run:cmd("switch default")

_ = test_run:cmd("switch autobootstrap1")
for i = 0, 99 do box.schema.space.create('space' .. tostring(i)):format({{'id', 'unsigned'}}) end
_ = test_run:cmd("switch autobootstrap2")
_ = test_run:cmd("switch autobootstrap3")

_ = test_run:cmd("switch autobootstrap1")
for i = 0, 99 do box.space['space' .. tostring(i)]:drop() end
_ = test_run:cmd("switch default")

--
-- Stop servers
--
test_run:drop_cluster(SERVERS)
