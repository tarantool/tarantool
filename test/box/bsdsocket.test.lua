type(box.socket)

box.socket('PF_INET', 'SOCK_STREAM', 'tcp121222');

s = box.socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:wait(.01)
type(s)
s:errno()
type(s:error())

s:nonblock(false)
s:sysconnect('127.0.0.1', string.gsub(box.cfg.primary_port, '^.*:', ''))
s:nonblock(true)
s:nonblock()
s:nonblock(false)
s:nonblock()
s:nonblock(true)

s:readable(.01)
s:wait(.01)
s:readable(0)
s:errno() > 0
s:error()
s:writable(.00000000000001)
s:writable(0)
s:wait(.01)

s:syswrite(box.pack('iii', 65280, 0, 12334))
s:readable(1)
s:wait(.01)
box.unpack('iii', s:sysread(4096))

s:syswrite(box.pack('iii', 65280, 0, 12335))
s:readable(1)
string.len(s:sysread(4096))
s:close()

s = box.socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:error()
s:bind('127.0.0.1', 3457)
s:error()
s:listen(128)
sevres = {}
type(box.fiber.wrap(function() s:readable() do local sc = s:accept() table.insert(sevres, sc) sc:syswrite('ok') sc:close() end end))
#sevres

sc = box.socket('PF_INET', 'SOCK_STREAM', 'tcp')
sc:nonblock(false)
sc:sysconnect('127.0.0.1', 3457)
sc:nonblock(true)
sc:readable(.5)
sc:sysread(4096)
string.match(tostring(sc), ', peer') ~= nil
#sevres
sevres[1].host

s:setsockopt('SOL_SOCKET', 'SO_BROADCAST', false)
s:getsockopt('SOL_SOCKET', 'SO_TYPE')
s:error()
s:setsockopt('SOL_SOCKET', 'SO_BSDCOMPAT', false)
s:setsockopt('SOL_SOCKET', 'SO_DEBUG', false)
s:getsockopt('SOL_SOCKET', 'SO_DEBUG')
s:setsockopt('SOL_SOCKET', 'SO_ACCEPTCONN', 1)
s:getsockopt('SOL_SOCKET', 'SO_RCVBUF') > 32
s:error()

s:linger()
s:linger(true, 1)
s:linger()
s:linger(false, 1)
s:linger()
s:shutdown('R')
s:close()

s = box.socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:bind('127.0.0.1', 3457)
s:listen(128)

sc = box.socket('PF_INET', 'SOCK_STREAM', 'tcp')

sc:writable()
sc:readable()
sc:sysconnect('127.0.0.1', 3457)
sc:writable(10)
sc:write('Hello, world')

sa = s:accept()
sa:nonblock(1)
sa:read(8)
sa:read(3)
sc:writable()
sc:write(', again')
sa:read(8)
sa:error()
string.len(sa:read(0))
type(sa:read(0))
sa:read(1, .01)
sc:writable()

sc:send('abc')
sa:read(3)

sc:send('Hello')
sa:readable()
sa:recv()
sa:recv()

sc:send('Hello')
sc:send(', world')
sc:send("\\nnew line")
sa:readline({'\\n'}, 1)
sa:readline(1, {'ine'}, 1)
sa:readline({'ine'}, 1)
sa:readline({'ine'}, 0.1)

sc:send('Hello, world')
sa:readline({','}, 1)
sc:shutdown('W')
sa:read(100, 1)
sa:read(100, 1)
sa:close()
sc:close()

s = box.socket('PF_UNIX', 'SOCK_STREAM', 'ip')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s ~= nil
s:nonblock()
s:nonblock(true)
s:nonblock()
os.remove('/tmp/tarantool-test-socket')
s:bind('unix/', '/tmp/tarantool-test-socket')
sc ~= nil
s:listen(1234)

sc = box.socket('PF_UNIX', 'SOCK_STREAM', 'ip')
sc:nonblock(true)
sc:sysconnect('unix/', '/tmp/tarantool-test-socket')
sc:error()

s:readable()
sa = s:accept()
sa:nonblock(true)
sa:send('Hello, world')
sc:recv()

sc:close()
sa:close()
s:close()

os.remove('/tmp/tarantool-test-socket')

--# setopt delimiter ';'
function aexitst(ai, host, port)
    for i, a in pairs(ai) do
        if a.host == host and a.port == port then
            return true
        end
    end
    return false
end;

aexitst( box.socket.getaddrinfo('localhost', 'http', {  protocol = 'tcp',
    type = 'SOCK_STREAM'}), '127.0.0.1', 80 );
--# setopt delimiter ''

#(box.socket.getaddrinfo('mail.ru', 'http', {})) > 0
wrong_addr = box.socket.getaddrinfo('mail12211alklkl.ru', 'http', {})
wrong_addr == nil or #wrong_addr == 0

sc = box.socket('PF_INET', 'SOCK_STREAM', 'tcp')
sc ~= nil
sc:getsockopt('SOL_SOCKET', 'SO_ERROR')
sc:nonblock(true)
sc:readable()
sc:sysconnect('127.0.0.1', 3458)
string.match(tostring(sc), ', peer') == nil
sc:writable()
string.match(tostring(sc), ', peer') == nil
box.errno.strerror(sc:getsockopt('SOL_SOCKET', 'SO_ERROR'))

--# setopt delimiter ';'
box.cjson.encode(box.socket.getaddrinfo('ya.ru', '80',
    { flags = { 'AI_NUMERICSERV', 'AI_NUMERICHOST', } }))
--# setopt delimiter ''

sc = box.socket('AF_INET', 'SOCK_STREAM', 'tcp')
box.cjson.encode(sc:name())
sc:nonblock(true)
sc:close()

s = box.socket('AF_INET', 'SOCK_DGRAM', 'udp')
s:bind('127.0.0.1', 3548)
sc = box.socket('AF_INET', 'SOCK_DGRAM', 'udp')
sc:sendto('127.0.0.1', 3548, 'Hello, world')
s:readable(10)
s:recv(4096)

sc:sendto('127.0.0.1', 3548, 'Hello, world, 2')
s:readable(10)
local d, from = s:recvfrom(4096) print(' - ', from.port > 0) from.port = 'Random port' return box.cjson.encode{d, from}
s:close()
sc:close()

s = box.socket('AF_INET', 'SOCK_DGRAM', 'udp')
s:nonblock(true)
s:bind('127.0.0.1')
s:name().port > 0
sc = box.socket('AF_INET', 'SOCK_DGRAM', 'udp')
sc:nonblock(true)
sc:sendto('127.0.0.1', s:name().port)
sc:sendto('127.0.0.1', s:name().port, 'Hello, World!')
s:readable(1)
data, from = s:recvfrom(10)
data
s:sendto(from.host, from.port, 'Hello, hello!')
sc:readable(1)
data_r, from_r = sc:recvfrom(4096)
data_r
from_r.host
from_r.port == s:name().port
s:close()
sc:close()
