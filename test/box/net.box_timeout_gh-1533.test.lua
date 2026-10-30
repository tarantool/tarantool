net = require('net.box')

-- Tarantool < 1.7.1 compatibility (gh-1533)
c = net.new(box.cfg.listen)
c:ping()
c:close()
