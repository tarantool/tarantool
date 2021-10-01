test_run = require('test_run').new()
fiber = require('fiber')

f = fiber.create(function() box.stat.net() end) f:wakeup()

old_listen = box.cfg.listen
new_listen = old_listen .. "A"
f = fiber.create(function() box.cfg{listen = new_listen} end) f:wakeup()
test_run:wait_cond(function() return box.cfg.listen == new_listen end)
box.cfg{listen = old_listen}
