-- preparatory stuff
fio = require('fio')
glob = fio.pathjoin(box.cfg.wal_dir, '*.xlog')
for _, file in pairs(fio.glob(glob)) do fio.unlink(file) end
glob = fio.pathjoin(box.cfg.wal_dir, '*.snap')
for _, file in pairs(fio.glob(glob)) do fio.unlink(file) end
--# stop server default
--# start server default
box.schema.user.grant('guest', 'replication')
_ = box.schema.space.create('test')
_ = box.space.test:create_index('pk')
--
-- reopen xlog
--
--# stop server default
--# start server default
box.space.test ~= nil
-- insert some stuff
-- 
box.space.test:auto_increment{'before snapshot'}
--
-- this snapshot will go to the replica
--
box.snapshot()
-- 
-- create a replica, let it catch up somewhat
--
--# create server replica with rpl_master=default, script='xlog/replica.lua'
--# start server replica
--# set connection replica
box.space.test:select{}
-- 
-- stop replica, restart the master, insert more stuff
-- which will make it into an xlog only
--
--# stop server replica
-- 
--# set connection default
box.space.test:auto_increment{'after snapshot'}
box.space.test:auto_increment{'after snapshot - one more row'}
--
-- save snapshot and remove xlogs
-- 
box.snapshot()
fio = require('fio')
glob = fio.pathjoin(box.cfg.wal_dir, '*.xlog')
files = fio.glob(glob)
for _, file in pairs(files) do fio.unlink(file) end
--# stop server default
--# start server default
--
-- make sure the server has some xlogs, otherwise the
-- replica doesn't discover the gap in the logs
--
box.space.test:auto_increment{'after snapshot and restart'}
box.space.test:auto_increment{'after snapshot and restart - one more row'}
--  
--  check that panic is true
--
box.cfg{panic_on_wal_error=true}
box.cfg.panic_on_wal_error
-- 
-- try to start the replica, ha-ha
-- (replication should fail, some rows are missing)
--
--# start server replica
--# set connection replica
fiber = require('fiber')
while box.info.replication.status ~= "stopped" do fiber.sleep(0.001) end
box.info.replication.status
box.info.replication.message
box.space.test:select{}
--
--
--# set connection default 
--# stop server replica
--# cleanup server replica
--
-- cleanup
box.space.test:drop()
box.schema.user.revoke('guest', 'replication')
