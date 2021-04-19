#!/usr/bin/env tarantool

--
-- Test that disabling feedback in initial configuration doesn't lead to
-- "attempt to index field 'cached_events' (a nil value)" errors when creating
-- or dropping a space/index.
--

local tap = require('tap')
local test = tap.test('feedback_enabled=false')

local ok, err = pcall(box.cfg, {feedback_enabled=false})

-- feedback daemon may be disabled at compile time. Then all options like
-- feedback_enabled will be undefined.
if not ok then
    test:plan(1)
    test:like(err, 'unexpected option', 'feedback_enabled is undefined')
    test:check()
    os.exit(0)
end

test:plan(2)
ok = pcall(box.schema.space.create, 'test')
test:ok(ok, 'space create succeeds')
ok = pcall(box.space.test.drop, box.space.test)
test:ok(ok, 'space drop succeeds')
test:check()
os.exit(0)
