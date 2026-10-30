replication_timeout = box.cfg.replication_timeout
replication_connect_timeout = box.cfg.replication_connect_timeout
box.cfg{replication_timeout=0.05, replication_connect_timeout=0.05, replication={}}

-- gh-3606 - Tarantool crashes if box.cfg.replication is updated concurrently
fiber = require('fiber')
c = fiber.channel(2)
f = function() fiber.create(function() pcall(box.cfg, {replication = {12345}}) c:put(true) end) end
f()
f()
c:get()
c:get()

box.cfg{replication = "", replication_timeout = replication_timeout, \
        replication_connect_timeout = replication_connect_timeout}
box.info.status
box.info.ro
