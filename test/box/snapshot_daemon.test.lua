fio = require 'fio'
errno = require 'errno'
fiber = require 'fiber'


PERIOD = 0.03
if jit.os ~= 'Linux' then PERIOD = 1.5 end


space = box.schema.space.create('snapshot_daemon')
index = space:create_index('pk', { type = 'tree', parts = { 1, 'num' }})


box.cfg{snapshot_period = PERIOD, snapshot_count = 2 }

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

--# setopt delimiter ';'

for i = 1, 100 do
    fiber.sleep(PERIOD)
    snaps = fio.glob(fio.pathjoin(box.cfg.snap_dir, '*.snap'))
    xlogs = fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))

    if #snaps == 2 then
        break
    end
end;

--# setopt delimiter ''



#snaps == 2 or snaps
#xlogs > 0

fio.basename(snaps[1], '.snap') >= fio.basename(xlogs[1], '.xlog')

-- restore default options
box.cfg{snapshot_period = 3600 * 4, snapshot_count = 4 }
space:drop()

box.cfg{ snapshot_count = .2 }

daemon = box.internal.snapshot_daemon
-- stop daemon
box.cfg{ snapshot_period = 0 }
-- wait daemon to stop
while daemon.fiber ~= nil do fiber.sleep(0) end
daemon.fiber == nil
-- start daemon
box.cfg{ snapshot_period = 10 }
daemon.fiber ~= nil
-- reload configuration
box.cfg{ snapshot_period = 15, snapshot_count = 20 }
daemon.snapshot_period == 15
daemon.snapshot_count = 20

-- stop daemon
box.cfg{ snapshot_count = 0 }
