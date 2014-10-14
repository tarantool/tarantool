#!/usr/bin/env tarantool
-- vim: set ft=lua :

-- see https://github.com/tarantool/tarantool/issues/583

tap = require 'tap'
fio = require 'fio'
log = require 'log'

tempdir = fio.tempdir()

box.cfg {
    wal_dir = tempdir,
    snap_dir = tempdir,
    sophia_dir = tempdir,
    logger      = fio.pathjoin(tempdir, 'tarantool.log')
}

local function test_replace(old_tuple, new_tuple)

end


box.schema.create_space('abc')
box.space.abc:create_index('pk', { type = 'tree' })
box.space.abc:on_replace(test_replace)


cleanup_list = fio.glob(fio.pathjoin(tempdir), '*')
for _, file in pairs(cleanup_list) do
    fio.unlink(file)
end
fio.rmdir(tempdir)
os.exit(0)

