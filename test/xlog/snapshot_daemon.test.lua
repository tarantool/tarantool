fio = require 'fio'
errno = require 'errno'
fiber = require 'fiber'
env = require('test_run')
test_run = env.new()

test_run:cleanup_cluster()


PERIOD = 0.03
if jit.os ~= 'Linux' then PERIOD = 1.5 end


space = box.schema.space.create('snapshot_daemon')
index = space:create_index('pk', { type = 'tree', parts = { 1, 'unsigned' }})


box.cfg{checkpoint_interval = PERIOD, checkpoint_count = 2 }

no = 1
-- first xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end
-- second xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end
-- wait for last snapshot
fiber.sleep(1.5 * PERIOD)
-- third xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end
-- fourth xlog
for i = 1, box.cfg.rows_per_wal + 10 do space:insert { no } no = no + 1 end

-- wait for last snapshot

test_run:cmd("setopt delimiter ';'")

for i = 1, 100 do
    fiber.sleep(PERIOD)
    snaps = fio.glob(fio.pathjoin(box.cfg.memtx_dir, '*.snap'))
    xlogs = fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))

    if #snaps == 2 then
        break
    end
end;

test_run:cmd("setopt delimiter ''");



#snaps == 2 or snaps
#xlogs > 0

fio.basename(snaps[1], '.snap') >= fio.basename(xlogs[1], '.xlog')

-- restore default options
box.cfg{checkpoint_interval = 3600 * 4, checkpoint_count = 4 }
space:drop()

daemon = box.internal.snapshot_daemon
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

-- stop daemon
box.cfg{ checkpoint_count = 0 }

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
