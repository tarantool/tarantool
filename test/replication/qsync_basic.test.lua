--
-- gh-4282: synchronous replication. It allows to make certain
-- spaces commit only when their changes are replicated to a
-- quorum of replicas.
--
s1 = box.schema.create_space('test1', {is_sync = true})
s1.is_sync
pk = s1:create_index('pk')
box.begin() s1:insert({1}) s1:insert({2}) box.commit()
s1:select{}

-- Default is async.
s2 = box.schema.create_space('test2')
s2.is_sync

-- Net.box takes sync into account.
box.schema.user.grant('guest', 'super')
netbox = require('net.box')
c = netbox.connect(box.cfg.listen)
c.space.test1.is_sync
c.space.test2.is_sync
c:close()
box.schema.user.revoke('guest', 'super')

s1:drop()
s2:drop()

-- Local space can't be synchronous.
box.schema.create_space('test', {is_sync = true, is_local = true})
