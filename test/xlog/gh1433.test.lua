fio = require('fio')
box.space._schema:insert({'gh1433'})
box.space._schema:delete({'gh1433'})
glob = fio.pathjoin(box.cfg.memtx_dir, '*.snap')
for _, file in pairs(fio.glob(glob)) do fio.unlink(file) end
box.snapshot()
