test_run = require('test_run').new()

--
-- gh-4605: replication and netbox both use URI as a remote
-- resource identifier. If URI does not contain a password, netbox
-- assumes it is an empty string - ''. But replication's applier
-- wasn't assuming the same, and just didn't send a password at
-- all, when it was not specified in the URI. It led to a strange
-- error message and inconsistent behaviour. The test checks, that
-- replication now also uses an empty string password by default.

box.schema.user.create('test_user', {password = ''})
box.schema.user.grant('test_user', 'replication')

test_run:cmd("create server replica_auth with rpl_master=default, script='replication/replica_auth.lua'")
test_run:cmd("start server replica_auth with wait=True, wait_load=True, args='test_user 0.1'")

test_run:switch('replica_auth')
i = box.info
i.replication[i.id % 2 + 1].upstream.status == 'follow' or i

test_run:switch('default')
test_run:cmd("stop server replica_auth")
test_run:cmd("cleanup server replica_auth")
test_run:cmd("delete server replica_auth")

box.schema.user.drop('test_user')
