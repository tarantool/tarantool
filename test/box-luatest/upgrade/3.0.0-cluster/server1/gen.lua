box.cfg{}
-- DDL is disabled until schema is upgraded (see gh-7149) so we have to grant
-- permissions required to run the test manually.
box.schema.user.grant('guest', 'super')

box.cfg{listen=3301, replication={3301, 3302, 3303}}
box.ctl.wait_rw()
box.snapshot()
