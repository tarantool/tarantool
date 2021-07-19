test_run = require('test_run').new()
fio = require('fio')
net = require('net.box')
console = require('console')
errinj = box.error.injection

console_sock_path = fio.pathjoin(fio.cwd(), 'console.sock')
_ = fio.unlink(console_sock_path)
s = console.listen(console_sock_path)
c = net.connect('unix/', console_sock_path, {console = true})

errinj.set('ERRINJ_NETBOX_IO_DELAY', true)
c:eval('return', 0)        -- timeout, but the request is still in flight
collectgarbage('collect')  -- force garbage collection of the request
errinj.set('ERRINJ_NETBOX_IO_DELAY', false)
c:eval('return')           -- ok

c:close()
s:close()
