digest = require'digest'
fiber = require'fiber'

_ = box.schema.space.create('snap'):create_index('pk')

-- write > 64 mb snapshot
for i = 0, 127 do box.space.snap:replace({i, digest.urandom(512 * 1024)}) end

t1 = fiber.time()
box.snapshot()
t2 = fiber.time()
t2 - t1 > 64 / box.cfg.snap_io_rate_limit * 0.95

box.space.snap:drop()
