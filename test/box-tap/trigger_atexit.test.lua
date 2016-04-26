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
    phia_dir = tempdir,
    logger      = fio.pathjoin(tempdir, 'tarantool.log'),
    slab_alloc_arena=0.1 -- for small systems
}

local function test_replace(old_tuple, new_tuple)

end


box.schema.space.create('abc')
box.space.abc:create_index('pk', { type = 'tree' })
box.space.abc:on_replace(test_replace)


cleanup_list = fio.glob(fio.pathjoin(tempdir, '*'))
for _, file in pairs(cleanup_list) do
    fio.unlink(file)
end
fio.rmdir(tempdir)
print("done")
os.exit(0)

