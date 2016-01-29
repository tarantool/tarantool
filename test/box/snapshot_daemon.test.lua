fio = require 'fio'
errno = require 'errno'
fiber = require 'fiber'
env = require('test_run')
test_run = env.new()


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

test_run:cmd("setopt delimiter ';'")

for i = 1, 100 do
    fiber.sleep(PERIOD)
    snaps = fio.glob(fio.pathjoin(box.cfg.snap_dir, '*.snap'))
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


--test for failed snapshot in case of insufficient descriptors

f=require('ffi')

test_run:cmd("setopt delimiter ';;;'")

f.cdef[[
struct rlimit {unsigned long cur; unsigned long max;};
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);

]];;;

test_run:cmd("setopt delimiter ''");;;

r = f.new('struct rlimit')

f.C.getrlimit(7, r)
orig_limit = r.cur;
r.cur = 1
f.C.setrlimit(7, r)
box.snapshot()
r.cur = orig_limit
f.C.setrlimit(7, r)

box.snapshot()

