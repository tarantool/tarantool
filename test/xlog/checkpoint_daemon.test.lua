fio = require 'fio'
errno = require 'errno'
fiber = require 'fiber'
env = require('test_run')
test_run = env.new()

test_run:cleanup_cluster()

box.cfg{checkpoint_interval = 0}

PERIOD = jit.os == 'Linux' and 0.03 or 1.5
WAIT_COND_TIMEOUT = 10

space = box.schema.space.create('checkpoint_daemon')
index = space:create_index('pk', { type = 'tree', parts = { 1, 'unsigned' }})

test_run:cmd("setopt delimiter ';'")

-- wait_snapshot* functions update these variables.
snaps = {};
xlogs = {};

-- Wait until tarantool creates a snapshot containing current
-- data slice.
function wait_snapshot(timeout)
    snaps = {}
    xlogs = {}
    local signature_str = tostring(box.info.signature)
    signature_str = string.rjust(signature_str, 20, '0')
    local exp_snap_filename = string.format('%s.snap', signature_str)
    return test_run:wait_cond(function()
        snaps = fio.glob(fio.pathjoin(box.cfg.memtx_dir, '*.snap'))
        xlogs = fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))
        return fio.basename(snaps[#snaps]) == exp_snap_filename
    end, timeout)
end;

-- Wait until snapshots count will be equal to the
-- checkpoint_count option.
function wait_snapshot_gc(timeout)
    snaps = {}
    xlogs = {}
    return test_run:wait_cond(function()
        snaps = fio.glob(fio.pathjoin(box.cfg.memtx_dir, '*.snap'))
        xlogs = fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))
        return #snaps == box.cfg.checkpoint_count
    end, timeout)
end;

test_run:cmd("setopt delimiter ''");

box.cfg{checkpoint_interval = PERIOD, checkpoint_count = 2 }

no = 1
-- first xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end
-- second xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end

wait_snapshot(WAIT_COND_TIMEOUT)

-- third xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end
-- fourth xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end

wait_snapshot(WAIT_COND_TIMEOUT)
wait_snapshot_gc(WAIT_COND_TIMEOUT)

#snaps == 2 or snaps
#xlogs > 0

-- gh-2780: check that a last snapshot mtime will be changed at
-- least two times.
test_run:cmd("setopt delimiter ';'")
last_mtime = fio.stat(snaps[#snaps]).mtime;
mtime_changes_cnt = 0;
test_run:wait_cond(function()
    local mtime = fio.stat(snaps[#snaps]).mtime
    if mtime ~= last_mtime then
        mtime_changes_cnt = mtime_changes_cnt + 1
        last_mtime = mtime
    end
    return mtime_changes_cnt == 2
end, WAIT_COND_TIMEOUT);
test_run:cmd("setopt delimiter ''");

-- restore default options
box.cfg{checkpoint_interval = 3600 * 4, checkpoint_count = 4 }
space:drop()

daemon = box.internal.checkpoint_daemon
-- stop daemon
box.cfg{ checkpoint_interval = 0 }
-- wait daemon to stop
while daemon.fiber ~= nil do fiber.sleep(0) end
daemon.fiber == nil
-- start daemon
box.cfg{ checkpoint_interval = 10 }
daemon.fiber ~= nil
-- reload configuration
box.cfg{ checkpoint_interval = 15, checkpoint_count = 20 }
daemon.checkpoint_interval == 15
daemon.checkpoint_count = 20

-- Check that checkpoint_count can't be < 1.
box.cfg{ checkpoint_count = 1 }
box.cfg{ checkpoint_count = 0 }
box.cfg.checkpoint_count

-- Start
PERIOD = 3600
box.cfg{ checkpoint_count = 2, checkpoint_interval = PERIOD}
snapshot_time, time  = daemon.next_snapshot_time, fiber.time()
snapshot_time + 1 >= time + PERIOD or {snapshot_time, time, PERIOD}
snapshot_time - 1 <= time + 2 * PERIOD or {snapshot_time, time, PERIOD}

daemon_fiber = daemon.fiber
daemon_control = daemon.control

-- Reload #1
PERIOD = 100
box.cfg{ checkpoint_count = 2, checkpoint_interval = PERIOD}
snapshot_time, time  = daemon.next_snapshot_time, fiber.time()
snapshot_time + 1 >= time + PERIOD or {snapshot_time, time, PERIOD}
snapshot_time - 1 <= time + 2 * PERIOD or {snapshot_time, time, PERIOD}
daemon.fiber == daemon_fiber
daemon.control == daemon_control

-- Reload #2
PERIOD = 1000
box.cfg{ checkpoint_count = 2, checkpoint_interval = PERIOD}
snapshot_time, time  = daemon.next_snapshot_time, fiber.time()
snapshot_time + 1 >= time + PERIOD or {snapshot_time, time, PERIOD}
snapshot_time - 1 <= time + 2 * PERIOD or {snapshot_time, time, PERIOD}
daemon.fiber == daemon_fiber
daemon.control == daemon_control

daemon_control = nil
daemin_fiber = nil

-- Shutdown
box.cfg{ checkpoint_count = 2, checkpoint_interval = 0}
daemon.next_snapshot_time
daemon.fiber == nil
daemon.control == nil
