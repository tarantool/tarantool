json = require 'json'
yaml = require 'yaml'
pickle = require 'pickle'
socket = require 'socket'
fiber = require 'fiber'
msgpack = require 'msgpack'
log = require 'log'
errno = require 'errno'
fio = require 'fio'
ffi = require('ffi')
type(socket)
env = require('test_run')
test_run = env.new()

socket('PF_INET', 'SOCK_STREAM', 'tcp121222');

s = socket('PF_INET', 'SOCK_STREAM', 'tcp')
type(s)
-- Invalid arguments
test_run:cmd("setopt delimiter ';'")
for k in pairs(getmetatable(s).__index) do
    local r, msg = pcall(s[k])
    if not msg:match('Usage:') then
        error("Arguments is not checked for "..k)
    end
end;
s:close();
test_run:cmd("setopt delimiter ''");

LISTEN = require('uri').parse(box.cfg.listen)
LISTEN ~= nil
s = socket.tcp_connect(LISTEN.host, LISTEN.service)
s:nonblock(true)
s:nonblock()
s:nonblock(false)
s:nonblock()
s:nonblock(true)

s:readable(.01)
s:wait(.01)
socket.iowait(s:fd(), 'RW')
socket.iowait(s:fd(), 3)
socket.iowait(s:fd(), 'R')
socket.iowait(s:fd(), 'r')
socket.iowait(s:fd(), 1)
socket.iowait(s:fd(), 'W')
socket.iowait(s:fd(), 'w')
socket.iowait(s:fd(), 2)
socket.iowait(s:fd(), '')
socket.iowait(s:fd(), -1)
socket.iowait(s:fd(), 'RW')
s:readable(0)
s:errno() > 0
s:error()
s:writable(.00000000000001)
s:writable(0)
s:wait(.01)

handshake = ffi.new('char[128]')
-- test sysread with char *
s:sysread(handshake, 128)
ffi.string(handshake, 9)

ping = msgpack.encode({ [0] = 64, [1] = 0 }) .. msgpack.encode({})
ping = msgpack.encode(string.len(ping)) .. ping

-- test syswrite with char *
s:syswrite(ffi.cast('const char *', ping), #ping)
s:readable(1)
s:wait(.01)

pong = s:sysread()
string.len(pong)
msgpack.decode(pong)
function remove_schema_id(t, x) if t[5] then t[5] = 'XXX' end return t, x end
remove_schema_id(msgpack.decode(pong, 6))

s:close()

s = socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:error()
s:bind('127.0.0.1', 3457)
s:error()
s:listen(128)
sevres = {}
type(require('fiber').create(function() s:readable() do local sc = s:accept() table.insert(sevres, sc) sc:syswrite('ok') sc:close() end end))
#sevres

sc = socket('PF_INET', 'SOCK_STREAM', 'tcp')
sc:nonblock(false)
sc:sysconnect('127.0.0.1', 3457)
sc:nonblock(true)
sc:readable(.5)
sc:sysread()
string.match(tostring(sc), ', peer') ~= nil
#sevres
sevres[1].host

s:setsockopt('SOL_SOCKET', 'SO_BROADCAST', false)
s:getsockopt('SOL_SOCKET', 'SO_TYPE')
s:error()
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
s:close()

s = socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:bind('127.0.0.1', 3457)
s:listen(128)

sc = socket('PF_INET', 'SOCK_STREAM', 'tcp')

sc:sysconnect('127.0.0.1', 3457) or errno() == errno.EINPROGRESS
sc:writable(10)
sc:write('Hello, world')

sa, addr = s:accept()
addr2 = sa:name()
addr2.host == addr.host
addr2.family == addr.family
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
sa:read('\\n', 1)
sa:read({ chunk = 1, delimiter = 'ine'}, 1)
sa:read('ine', 1)
sa:read('ine', 0.1)

sc:send('Hello, world')
sa:read(',', 1)
sc:shutdown('W')
sa:close()
sc:close()

s = socket('PF_UNIX', 'SOCK_STREAM', 0)
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s ~= nil
s:nonblock()
s:nonblock(true)
s:nonblock()
os.remove('/tmp/tarantool-test-socket')
s:bind('unix/', '/tmp/tarantool-test-socket')
sc ~= nil
s:listen(1234)

sc = socket('PF_UNIX', 'SOCK_STREAM', 0)
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

test_run:cmd("setopt delimiter ';'")
function aexitst(ai, hostnames, port)
    for i, a in pairs(ai) do
        for j, host in pairs(hostnames) do
            if a.host == host and a.port == port then
                return true
            end
        end
    end
    return ai
end;


aexitst( socket.getaddrinfo('localhost', 'http', {  protocol = 'tcp',
    type = 'SOCK_STREAM'}), {'127.0.0.1', '::1'}, 80 );
test_run:cmd("setopt delimiter ''");

#(socket.getaddrinfo('tarantool.org', 'http', {})) > 0
wrong_addr = socket.getaddrinfo('non-existing-domain-name-12211alklkl.com', 'http', {})
wrong_addr == nil or #wrong_addr == 0

sc = socket('PF_INET', 'SOCK_STREAM', 'tcp')
sc ~= nil
sc:getsockopt('SOL_SOCKET', 'SO_ERROR')
sc:nonblock(true)
sc:sysconnect('127.0.0.1', 3458) or errno() == errno.EINPROGRESS or errno() == errno.ECONNREFUSED
string.match(tostring(sc), ', peer') == nil
sc:writable()
string.match(tostring(sc), ', peer') == nil
socket_error = sc:getsockopt('SOL_SOCKET', 'SO_ERROR')
socket_error == errno.ECONNREFUSED or socket_error == 0

test_run:cmd("setopt delimiter ';'")
socket.getaddrinfo('127.0.0.1', '80', { type = 'SOCK_DGRAM',
    flags = { 'AI_NUMERICSERV', 'AI_NUMERICHOST', } });
test_run:cmd("setopt delimiter ''");

sc = socket('AF_INET', 'SOCK_STREAM', 'tcp')
json.encode(sc:name())
sc:name()
sc:nonblock(true)
sc:close()

s = socket('AF_INET', 'SOCK_DGRAM', 'udp')
s:bind('127.0.0.1', 3548)
sc = socket('AF_INET', 'SOCK_DGRAM', 'udp')
sc:sendto('127.0.0.1', 3548, 'Hello, world')
s:readable(10)
s:recv()

sc:sendto('127.0.0.1', 3548, 'Hello, world, 2')
s:readable(10)
d, from = s:recvfrom()
from.port > 0
from.port = 'Random port'
json.encode{d, from}
s:close()
sc:close()

s = socket('AF_INET', 'SOCK_DGRAM', 'udp')
s:nonblock(true)
s:bind('127.0.0.1')
s:name().port > 0
sc = socket('AF_INET', 'SOCK_DGRAM', 'udp')
sc:nonblock(true)
sc:sendto('127.0.0.1', s:name().port)
sc:sendto('127.0.0.1', s:name().port, 'Hello, World!')
s:readable(1)
data, from = s:recvfrom(10)
data
s:sendto(from.host, from.port, 'Hello, hello!')
sc:readable(1)
data_r, from_r = sc:recvfrom()
data_r
from_r.host
from_r.port == s:name().port
s:close()
sc:close()

-- tcp_connect

s = socket.tcp_connect('tarantool.org', 80)
string.match(tostring(s), ', aka') ~= nil
string.match(tostring(s), ', peer') ~= nil
s:write("HEAD / HTTP/1.0\r\nHost: tarantool.org\r\n\r\n")
header = s:read({chunk = 4000, delimiter = {"\n\n", "\r\n\r\n" }}, 1)
string.match(header, "\r\n\r\n$") ~= nil
string.match(header, "200 [Oo][Kk]") ~= nil
s:close()

socket.tcp_connect('127.0.0.1', 80, 0.00000000001)

-- AF_INET
port = 35490
s = socket('AF_INET', 'SOCK_STREAM', 'tcp')
s:bind('127.0.0.1', port)
s:listen()
sc, e = socket.tcp_connect('127.0.0.1', port), errno()
sc ~= nil
e == 0
sc:close()
s:close()
socket.tcp_connect('127.0.0.1', port), errno() == errno.ECONNREFUSED

-- AF_UNIX
path = '/tmp/tarantool-test-socket'
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
s:bind('unix/', path)
socket.tcp_connect('unix/', path), errno() == errno.ECONNREFUSED
s:listen()
sc, e = socket.tcp_connect('unix/', path), errno()
sc ~= nil
e
sc:close()
s:close()
socket.tcp_connect('unix/', path), errno() == errno.ECONNREFUSED
os.remove(path)
socket.tcp_connect('unix/', path), errno() == errno.ENOENT

-- invalid fd
s = socket('AF_INET', 'SOCK_STREAM', 'tcp')
s:read(9)
s:close()
s.socket.fd = 512
tostring(s)
s:readable(0)
s:writable(0)
s = nil

-- close
port = 65454
serv = socket('AF_INET', 'SOCK_STREAM', 'tcp')
serv:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
serv:bind('127.0.0.1', port)
serv:listen()
test_run:cmd("setopt delimiter ';'")
f = fiber.create(function(serv)
    serv:readable()
    sc = serv:accept()
    sc:write("Tarantool test server")
    sc:shutdown()
    sc:close()
    serv:close()
end, serv);
test_run:cmd("setopt delimiter ''");

s = socket.tcp_connect('127.0.0.1', port)
ch = fiber.channel()
f = fiber.create(function() s:read(12) ch:put(true) end)
s:close()
ch:get(1)
s:error()

-- random port
master = socket('PF_INET', 'SOCK_STREAM', 'tcp')
master:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
port = 32768 + math.random(32768)
attempt = 0
test_run:cmd("setopt delimiter ';'")
while attempt < 10 do
    if not master:bind('127.0.0.1', port)  then
        port = 32768 + math.random(32768)
        attempt = attempt + 1
    else
        break
    end
end;
master:listen();
function gh361()
    local s = socket('PF_INET', 'SOCK_STREAM', 'tcp')
    s:sysconnect('127.0.0.1', port)
    s:wait()
    res = s:read(1200)
end;
test_run:cmd("setopt delimiter ''");
f = fiber.create(gh361)
fiber.cancel(f)
while f:status() ~= 'dead' do fiber.sleep(0.001) end
master:close()
f = nil


path = '/tmp/tarantool-test-socket'
s = socket('PF_UNIX', 'SOCK_STREAM', 0)
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:error()
s:bind('unix/', path)
s:error()
s:listen(128)
test_run:cmd("setopt delimiter ';'")
f = fiber.create(function()
    for i=1,2 do
        s:readable()
        local sc = s:accept()
        sc:write('ok!')
        sc:shutdown()
        sc:close()
    end
end);
test_run:cmd("setopt delimiter ''");

c = socket.tcp_connect('unix/', path)
c:error()
x = c:read('!')
x, type(x), #x
x = c:read('!')
c:error()
x, type(x), #x
x = c:read('!')
c:error()
x, type(x), #x
c:close()

c = socket.tcp_connect('unix/', path)
c:error()
x = c:read(3)
c:error()
x, type(x), #x
x = c:read(1)
c:error()
x, type(x), #x
x = c:read(1)
c:error()
x, type(x), #x
x = c:sysread(1)
c:error()
x, type(x), #x
c:close()

s:close()

os.remove(path)

server, addr = socket.tcp_server('unix/', path, function(s) s:write('Hello, world') end)
type(addr)
server ~= nil
fiber.sleep(.5)
client = socket.tcp_connect('unix/', path)
client ~= nil
client:read(123)
server:close()
-- unix socket automatically removed
collectgarbage('collect')
fio.stat(path) == nil

test_run:cmd("setopt delimiter ';'")
server, addr = socket.tcp_server('localhost', 0, { handler = function(s)
    s:read(2)
    s:write('Hello, world')
end, name = 'testserv'});
test_run:cmd("setopt delimiter ''");
type(addr)
server ~= nil
addr2 = server:name()
addr.host == addr2.host
addr.family == addr2.family
fiber.sleep(.5)
client = socket.tcp_connect(addr2.host, addr2.port)
client ~= nil
-- Check that listen and client fibers have appropriate names
cnt = 0
test_run:cmd("setopt delimiter ';'")
for i=100,900 do
    local f = fiber.find(i)
    if f and f:name():match('^testserv/') then
        cnt = cnt + 1
    end
end;
test_run:cmd("setopt delimiter ''");
cnt
client:write('hi')
client:read(123)
client:close()
server:close()

longstring = string.rep("abc", 65535)
server = socket.tcp_server('unix/', path, function(s) s:write(longstring) end)
client = socket.tcp_connect('unix/', path)
client:read(#longstring) == longstring
client = socket.tcp_connect('unix/', path)
client:read(#longstring + 1) == longstring
client = socket.tcp_connect('unix/', path)
client:read(#longstring - 1) == string.sub(longstring, 1, #longstring - 1)
longstring = "Hello\r\n\r\nworld\n\n"
client = socket.tcp_connect('unix/', path)
client:read{ line = { "\n\n", "\r\n\r\n" } }
server:close()

-- gh-658: socket:read() incorrectly handles size and delimiter together
body = "a 10\nb 15\nabc"
remaining = #body
test_run:cmd("setopt delimiter ';'")
server = socket.tcp_server('unix/', path, function(s)
    s:write(body)
    s:read(100500)
end);
test_run:cmd("setopt delimiter ''");
client = socket.tcp_connect('unix/', path)
buf = client:read({ size = remaining, delimiter = "\n"})
buf == "a 10\n"
remaining = remaining - #buf
buf = client:read({ size = remaining, delimiter = "\n"})
buf == "b 15\n"
remaining = remaining - #buf
buf = client:read({ size = remaining, delimiter = "\n"})
buf == "abc"
remaining = remaining - #buf
remaining == 0
buf = client:read({ size = remaining, delimiter = "\n"})
buf == ""
buf = client:read({ size = remaining, delimiter = "\n"})
buf == ""
client:close()
server:close()

-- Test that socket is closed on GC
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
s:bind('unix/', path)
s:listen()
s = nil
collectgarbage('collect')
collectgarbage('collect')
socket.tcp_connect('unix/', path), errno() == errno.ECONNREFUSED
os.remove(path)

-- Test serializers with sockets
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
-- check __serialize hook
json.decode(json.encode(s)).fd == s:fd()
yaml.decode(yaml.encode(s)).fd == s:fd()
s = nil

-- start AF_UNIX server with dead socket exists
path = '/tmp/tarantool-test-socket'
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
s:bind('unix/', path)
s:close()

s = socket('AF_UNIX', 'SOCK_STREAM', 0)
{ s:bind('unix/', path), errno.strerror() }
s:close()

s = socket.tcp_server('unix/', path, function() end)
s ~= nil
s:close()
fio.stat(path) == nil

{ socket.tcp_connect('abrakadabra#123') == nil, errno.strerror() }


-- wrong options for getaddrinfo
socket.getaddrinfo('host', 'port', { type = 'WRONG' }) == nil and errno() == errno.EINVAL
socket.getaddrinfo('host', 'port', { family = 'WRONG' }) == nil and errno() == errno.EINVAL
socket.getaddrinfo('host', 'port', { protocol = 'WRONG' }) == nil and errno() == errno.EINVAL
socket.getaddrinfo('host', 'port', { flags = 'WRONG' }) == nil and errno() == errno.EINVAL

-- gh-574: check that fiber with getaddrinfo can be safely cancelled
test_run:cmd("setopt delimiter ';'")
f = fiber.create(function()
    while true do
        local result = socket.getaddrinfo('localhost', '80')
        fiber.sleep(0)
    end
end);
test_run:cmd("setopt delimiter ''");
f:cancel()
