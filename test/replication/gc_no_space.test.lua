--
-- This test checks that when the WAL thread runs out of disk
-- space it automatically deletes old WAL files and notifies
-- the TX thread so that the latter can shoot off WAL consumers
-- that need them. See gh-3397.
--
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
vclock_diff = require('fast_replica').vclock_diff

fio = require('fio')
errinj = box.error.injection

test_run:cmd("setopt delimiter ';'")
function wait_file_count(dir, glob, count)
    return test_run:wait_cond(function()
        local files = fio.glob(fio.pathjoin(dir, glob))
        if #files == count then
            return true
        end
        return false, files
    end)
end;
function check_wal_count(count)
    return wait_file_count(box.cfg.wal_dir, '*.xlog', count)
end;
function check_snap_count(count)
    return wait_file_count(box.cfg.memtx_dir, '*.snap', count)
end;
test_run:cmd("setopt delimiter ''");

box.cfg{checkpoint_count = 2}

test_run:cleanup_cluster()
box.schema.user.grant('guest', 'replication')
s = box.schema.space.create('test', {engine = engine})
_ = s:create_index('pk')
box.snapshot()

--
-- Create a few dead replicas to pin WAL files.
--
test_run:cmd("create server replica with rpl_master=default, script='replication/replica.lua'")
test_run:cmd("start server replica")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

s:auto_increment{}
box.snapshot()

test_run:cmd("start server replica")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")

s:auto_increment{}
box.snapshot()

test_run:cmd("start server replica")
test_run:cmd("stop server replica")
test_run:cmd("cleanup server replica")
test_run:cmd("delete server replica")

--
-- Make a few checkpoints and check that old WAL files are not
-- deleted.
--
s:auto_increment{}
box.snapshot()
s:auto_increment{}
box.snapshot()
s:auto_increment{}

check_wal_count(5)
check_snap_count(2)
gc = box.info.gc()
#gc.consumers -- 3
#gc.checkpoints -- 2
vclock_diff(gc.vclock, gc.consumers[1].vclock) -- 0

--
-- Inject a ENOSPC error and check that the WAL thread deletes
-- old WAL files to prevent the user from seeing the error.
--
errinj.set('ERRINJ_WAL_FALLOCATE', 2)
s:auto_increment{} -- success
errinj.info()['ERRINJ_WAL_FALLOCATE'].state -- 0

check_wal_count(3)
check_snap_count(2)
gc = box.info.gc()
#gc.consumers -- 1
#gc.checkpoints -- 2
vclock_diff(gc.vclock, gc.consumers[1].vclock) -- 0

--
-- Check that the WAL thread never deletes WAL files that are
-- needed for recovery from the last checkpoint, but may delete
-- older WAL files that would be kept otherwise for recovery
-- from backup checkpoints.
--
errinj.set('ERRINJ_WAL_FALLOCATE', 3)
s:auto_increment{} -- failure
errinj.info()['ERRINJ_WAL_FALLOCATE'].state -- 0

check_wal_count(1)
check_snap_count(2)
gc = box.info.gc()
#gc.consumers -- 0
#gc.checkpoints -- 2
gc.signature == gc.checkpoints[2].signature

s:drop()
box.schema.user.revoke('guest', 'replication')
test_run:cleanup_cluster()

-- Check that the garbage collector vclock is recovered correctly.
test_run:cmd("restart server default")
gc = box.info.gc()
#gc.checkpoints -- 2
gc.signature == gc.checkpoints[2].signature
