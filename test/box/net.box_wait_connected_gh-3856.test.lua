net = require('net.box')

--
-- gh-3856: wait_connected = false is ignored.
--
c = net.connect('8.8.8.8:123456', {wait_connected = false})
c
c:close()
