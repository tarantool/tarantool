#!/usr/bin/env tarantool
-- vim: set ft=lua :

-- see https://github.com/tarantool/tarantool/issues/583

local fio = require 'fio'

local tempdir = fio.tempdir()

box.cfg {
    wal_dir = tempdir,
    memtx_dir = tempdir,
    vinyl_dir = tempdir,
    log       = fio.pathjoin(tempdir, 'tarantool.log'),
    memtx_memory = 104857600 -- for small systems
}

local function test_replace()

end


box.schema.space.create('abc')
box.space.abc:create_index('pk', { type = 'tree' })
box.space.abc:on_replace(test_replace)


local cleanup_list = fio.glob(fio.pathjoin(tempdir, '*'))
for _, file in pairs(cleanup_list) do
    fio.unlink(file)
end
fio.rmdir(tempdir)
print("done")
os.exit(0)

