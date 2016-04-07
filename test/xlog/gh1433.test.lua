fio = require('fio')
glob = fio.pathjoin(box.cfg.snap_dir, '*.snap')
for _, file in pairs(fio.glob(glob)) do fio.unlink(file) end
box.snapshot()
