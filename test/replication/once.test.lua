fiber = require('fiber')

box.once()
box.once("key")
box.once("key", "key")
box.once("key", nil)
box.once("key", function() end)

once  = nil
function f(arg) if once ~= nil then once = once + arg else once = arg end end
box.once("test", f, 1)
once
box.once("test", f, 1)
once

-- Check that box.once() does not fail if the instance is read-only,
-- instead it waits until the instance enters read-write mode.
once = nil
box.cfg{read_only = true}
ch = fiber.channel(1)
_ = fiber.create(function() box.once("ro", f, 1) ch:put(true) end)
fiber.sleep(0.001)
once -- nil
box.cfg{read_only = false}
ch:get()
once -- 1
box.cfg{read_only = true}
box.once("ro", f, 1) -- ok, already done
once -- 1
box.cfg{read_only = false}
box.space._schema:delete{"oncero"}
box.space._schema:delete{"oncekey"}
box.space._schema:delete{"oncetest"}
