test_run = require('test_run').new()
--
-- gh-4606: the admin user has universal privileges before
-- bootstrap or recovery are done. That allows to, for example,
-- bootstrap from a remote master, because to do that the admin
-- should be able to insert into system spaces, such as _priv.
--
-- But the admin could lost its universal access if, for
-- example, a role was granted to him before universal access was
-- recovered. Because any change in access rights, even in granted
-- roles, led to rebuild of universal access.
--
box.schema.user.passwd('admin', '111')
box.schema.user.grant('admin', 'replication')
test_run:cmd("create server replica_auth with rpl_master=default, script='replication/replica_auth.lua'")
test_run:cmd("start server replica_auth with args='admin:111 0.1'")
test_run:switch('replica_auth')
i = box.info
replica_id = i.id % 2 + 1
test_run:wait_upstream(replica_id, {status = 'follow'}) or i

test_run:switch('default')
test_run:cmd("stop server replica_auth")
test_run:cmd("cleanup server replica_auth")
test_run:cmd("delete server replica_auth")

box.schema.user.passwd('admin', '')
box.schema.user.revoke('admin', 'replication')
