test_run = require('test_run').new()
test_run:cmd('restart server default with cleanup=1')

fio = require('fio')

test_run:cmd("setopt delimiter ';'")
function dump_header(path)
    local f = io.open(path)
    local header = {}
    while true do
        local line = f:read()
        if line == "" then break end
        table.insert(header, line)
    end
    f:close()
    return header
end;
test_run:cmd("setopt delimiter ''");
test_run:cmd("push filter '"..box.info.uuid.."' to '<instance_uuid>'")
test_run:cmd("push filter '".._TARANTOOL.."' to '<version>'")

checkpoint_lsn = box.info.lsn

-- SNAP files
snap_name = string.format("%020d.snap", checkpoint_lsn)
dump_header(fio.pathjoin(box.cfg.memtx_dir, snap_name))

-- XLOG files
box.space._schema:insert({"layout_test"})
xlog_name = string.format("%020d.xlog", checkpoint_lsn)
dump_header(fio.pathjoin(box.cfg.wal_dir, xlog_name))
box.space._schema:delete({"layout_test"})

box.snapshot()
checkpoint_lsn = box.info.lsn

-- SNAP files
snap_name = string.format("%020d.snap", checkpoint_lsn)
dump_header(fio.pathjoin(box.cfg.memtx_dir, snap_name))

-- XLOG files
box.space._schema:insert({"layout_test"})
xlog_name = string.format("%020d.xlog", checkpoint_lsn)
dump_header(fio.pathjoin(box.cfg.wal_dir, xlog_name))
box.space._schema:delete({"layout_test"})

box.snapshot()
checkpoint_lsn = box.info.lsn

-- SNAP files
snap_name = string.format("%020d.snap", checkpoint_lsn)
dump_header(fio.pathjoin(box.cfg.memtx_dir, snap_name))

-- XLOG files
box.space._schema:insert({"layout_test"})
xlog_name = string.format("%020d.xlog", checkpoint_lsn)
dump_header(fio.pathjoin(box.cfg.wal_dir, xlog_name))
box.space._schema:delete({"layout_test"})

test_run:cmd("clear filter")
