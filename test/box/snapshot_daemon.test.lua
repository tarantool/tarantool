fio = require 'fio'
errno = require 'errno'
fiber = require 'fiber'


PERIOD = 0.03

--# setopt delimiter ';'

ffi = require 'ffi'
ffi.cdef[[int uname(char *buf)]]

function uname()
    local name = ffi.new('char[?]', 4096)
    ffi.C.uname(name)
    return ffi.string(name)
end

if uname() ~= 'Linux' then
    PERIOD = 1.5
end

--# setopt delimiter ''


space = box.schema.create_space('snapshot_daemon')
space:create_index('pk', { type = 'tree', parts = { 1, 'num' }})


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

PERIOD
