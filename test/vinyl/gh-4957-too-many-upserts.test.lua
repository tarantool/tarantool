test_run = require('test_run').new()
test_run:cmd("push filter 'Invalid VYLOG file: Slice [0-9]+ deleted but not registered'" .. \
             "to 'Invalid VYLOG file: Slice <NUM> deleted but not registered'")

s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
s:insert{1, 1}
box.snapshot()

-- Let's test number of upserts in one transaction that exceeds
-- the limit of operations allowed in one update.
--
ups_cnt = 5000
box.begin()
for i = 1, ups_cnt do s:upsert({1}, {{'&', 2, 1}}) end
box.commit()
-- Upserts are not able to squash, so scheduler will get stuck.
-- So let's not waste much time here, just check that no crash
-- takes place.
--
box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', 0.001)
box.snapshot()

fiber = require('fiber')
fiber.sleep(0.01)

s:drop()

s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')

tuple = {}
for i = 1, ups_cnt do tuple[i] = i end
_ = s:insert(tuple)
box.snapshot()

box.begin()
for k = 1, ups_cnt do s:upsert({1}, {{'+', k, 1}}) end
box.commit()
box.snapshot()
fiber.sleep(0.01)

box.error.injection.set('ERRINJ_VY_SCHED_TIMEOUT', 0)
s:drop()

-- restart the current server to resolve the issue #5141
-- which reproduced in test:
--   vinyl/gh-5141-invalid-vylog-file.test.lua
test_run:cmd("restart server default with cleanup=True")


