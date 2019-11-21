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
test_run:cmd("push filter '(error: .builtin/.*[.]lua):[0-9]+' to '\\1'")

TIMEOUT = 100

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

s:readable(TIMEOUT)
s:wait(TIMEOUT)
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
socket.iowait(s:fd(), 'RW', -100500)
s:readable(0)
s:errno() > 0
s:error()
s:writable(.00000000000001)
s:writable(0)
s:wait(TIMEOUT)
socket.iowait(nil, nil, -1)
socket.iowait(nil, nil, 0.0001)
socket.iowait(-1, nil, 0.0001)
socket.iowait(nil, 'RW')
socket.iowait(0, nil)

handshake = ffi.new('char[128]')
-- test sysread with char *
s:sysread(handshake, 128)
ffi.string(handshake, 9)

ping = msgpack.encode({ [0] = 64, [1] = 0 })
ping = msgpack.encode(string.len(ping)) .. ping

-- test syswrite with char *
s:syswrite(ffi.cast('const char *', ping), #ping)
s:readable(TIMEOUT)
s:wait(TIMEOUT)

pong = s:sysread()
string.len(pong)
msgpack.decode(pong)
function remove_schema_id(t, x) if t[5] then t[5] = 'XXX' end return t, x end
remove_schema_id(msgpack.decode(pong, 6))

s:close()

s = socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:error()
s:bind('127.0.0.1', 0)
s:error()
s:listen(128)
sevres = {}
type(require('fiber').create(function() s:readable() do local sc = s:accept() table.insert(sevres, sc) sc:syswrite('ok') sc:close() end end))
#sevres

sc = socket('PF_INET', 'SOCK_STREAM', 'tcp')
sc:nonblock(false)
sc:sysconnect('127.0.0.1', s:name().port)
sc:nonblock(true)
sc:readable(TIMEOUT)
sc:sysread()
string.match(tostring(sc), ', peer') ~= nil
#sevres
sevres[1].host

s:setsockopt('SOL_SOCKET', 'SO_BROADCAST', false)
s:getsockopt('socket', 'SO_TYPE')
s:error()
s:setsockopt('SOL_SOCKET', 'SO_DEBUG', false)
s:getsockopt('socket', 'SO_DEBUG')
s:setsockopt('SOL_SOCKET', 'SO_ACCEPTCONN', 1)
s:getsockopt('SOL_SOCKET', 'SO_RCVBUF') > 32
s:error()
s:setsockopt('IPPROTO_TCP', 'TCP_NODELAY', true)
s:getsockopt('IPPROTO_TCP', 'TCP_NODELAY') > 0
s:setsockopt('SOL_TCP', 'TCP_NODELAY', false)
s:getsockopt('SOL_TCP', 'TCP_NODELAY') == 0
s:setsockopt('tcp', 'TCP_NODELAY', true)
s:getsockopt('tcp', 'TCP_NODELAY') > 0
s:setsockopt(6, 'TCP_NODELAY', false)
s:getsockopt(6, 'TCP_NODELAY') == 0
not s:setsockopt(nil, 'TCP_NODELAY', true) and errno() == errno.EINVAL
not s:getsockopt(nil, 'TCP_NODELAY') and errno() == errno.EINVAL

s:linger()
s:linger(true, 1)
s:linger()
s:linger(false, 1)
s:linger()
s:close()

s = socket('PF_INET', 'SOCK_STREAM', 'tcp')
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s:bind('127.0.0.1', 0)
s:listen(128)

sc = socket('PF_INET', 'SOCK_STREAM', 'tcp')

sc:sysconnect('127.0.0.1', s:name().port) or errno() == errno.EINPROGRESS
sc:writable(TIMEOUT)
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

-- gh-3979 Check for errors when argument is negative.

sc:read(-1)
sc:sysread(-1)
sc:read(-100)
sc:sysread(-100)

sc:send('abc')
sa:read(3)

sc:send('Hello')
sa:readable()
sa:recv()
sa:recv()

sc:send('Hello')
sc:send(', world')
sc:send("\\nnew line")
sa:read('\\n', TIMEOUT)
sa:read({ chunk = 1, delimiter = 'ine'}, 1)
sa:read('ine', TIMEOUT)
sa:read('ine', 0.1)

sc:send('Hello, world')
sa:read(',', TIMEOUT)
sc:shutdown('W')
sa:close()
sc:close()

s = socket('PF_UNIX', 'SOCK_STREAM', 0)
s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
s ~= nil
s:nonblock()
s:nonblock(true)
s:nonblock()
path = 'tarantool-test-socket'
os.remove(path)
s:bind('unix/', path)
sc ~= nil
s:listen(1234)

sc = socket('PF_UNIX', 'SOCK_STREAM', 0)
sc:nonblock(true)
sc:sysconnect('unix/', path)
sc:error()

s:readable()
sa = s:accept()
sa:nonblock(true)
sa:send('Hello, world')
sc:recv()

sc:close()
sa:close()
s:close()

_ = os.remove(path)

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
inf = socket.getaddrinfo('127.0.0.1', '80', { type = 'SOCK_DGRAM',
    flags = { 'AI_NUMERICSERV', 'AI_NUMERICHOST', } });
-- musl libc https://github.com/tarantool/tarantool/issues/1249
inf[1].canonname = nil;
inf;
test_run:cmd("setopt delimiter ''");

sc = socket('AF_INET', 'SOCK_STREAM', 'tcp')
json.encode(sc:name())
sc:name()
sc:nonblock(true)
sc:close()

s = socket('AF_INET', 'SOCK_DGRAM', 'udp')
s:bind('127.0.0.1', 0)
sc = socket('AF_INET', 'SOCK_DGRAM', 'udp')
sc:sendto('127.0.0.1', s:name().port, 'Hello, world')
s:readable(TIMEOUT)
s:recv()

sc:sendto('127.0.0.1', s:name().port, 'Hello, world, 2')
s:readable(TIMEOUT)
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
s:readable(TIMEOUT)
data, from = s:recvfrom(10)
data
s:sendto(from.host, from.port, 'Hello, hello!')
sc:readable(TIMEOUT)
data_r, from_r = sc:recvfrom()
data_r
from_r.host
from_r.port == s:name().port
s:close()
sc:close()

-- tcp_connect

-- Test timeout. In this test, tcp_connect can return the second
-- output value from internal.getaddrinfo (usually on Mac OS, but
-- theoretically it can happen on Linux too). Sometimes
-- getaddrinfo() is timed out, sometimes connect. On Linux however
-- getaddrinfo is fast enough to never give timeout error in
-- the case. So, there are two sources of timeout errors that are
-- reported differently. This difference has appeared after
-- gh-4138 patch.
s, err = socket.tcp_connect('127.0.0.1', 80, 0.00000000001)
s == nil

-- AF_INET
s = socket('AF_INET', 'SOCK_STREAM', 'tcp')
s:bind('127.0.0.1', 0)
port = s:name().port
s:listen()
sc, e = socket.tcp_connect('127.0.0.1', port), errno()
sc ~= nil
e == 0
sc:close()
s:close()
socket.tcp_connect('127.0.0.1', port), errno() == errno.ECONNREFUSED

-- AF_UNIX
path = 'tarantool-test-socket'
_ = os.remove(path)
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
_ = os.remove(path)
socket.tcp_connect('unix/', path), errno() == errno.ENOENT

-- invalid fd / tampering
s = socket('AF_INET', 'SOCK_STREAM', 'tcp')
s:read(9)
s:close()
s._gc_socket.fd = 512
s._gc_socket = nil
tostring(s)
s = nil

-- random port
master = socket('PF_INET', 'SOCK_STREAM', 'tcp')
master:bind('127.0.0.1', 0)
port = master:name().port
master:listen()
test_run:cmd("setopt delimiter ';'")
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


path = 'tarantool-test-socket'
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

_ = os.remove(path)

server, addr = socket.tcp_server('unix/', path, function(s) s:write('Hello, world') end)
type(addr)
server ~= nil
fiber.sleep(.1)
client = socket.tcp_connect('unix/', path)
client ~= nil
client:read(123)
server:close()
-- unix socket automatically removed
while fio.stat(path) ~= nil do fiber.sleep(0.001) end

test_run:cmd("setopt delimiter ';'")
server, addr = socket.tcp_server('127.0.0.1', 0, { handler = function(s)
    s:read(2)
    s:write('Hello, world')
end, name = 'testserv'});
test_run:cmd("setopt delimiter ''");
type(addr)
server ~= nil
addr2 = server:name()
addr.host == addr2.host
addr.family == addr2.family
fiber.sleep(.1)
client = socket.tcp_connect(addr2.host, addr2.port)
client ~= nil
-- Check that listen and client fibers have appropriate names
cnt = 0
test_run:cmd("setopt delimiter ';'")
for _, f in pairs(fiber.info()) do
    if f.name:match('^testserv/') then
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
_ = os.remove(path)

-- Test that socket is closed on GC
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
s:bind('unix/', path)
s:listen()
s = nil
while socket.tcp_connect('unix/', path) do collectgarbage('collect') end
_ = os.remove(path)

-- Test serializers with sockets
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
-- check __serialize hook
json.decode(json.encode(s)).fd == s:fd()
yaml.decode(yaml.encode(s)).fd == s:fd()
s = nil

-- start AF_UNIX server with dead socket exists
path = 'tarantool-test-socket'
s = socket('AF_UNIX', 'SOCK_STREAM', 0)
s:bind('unix/', path)
s:close()

s = socket('AF_UNIX', 'SOCK_STREAM', 0)
{ s:bind('unix/', path), errno() == errno.EADDRINUSE }
s:close()

s = socket.tcp_server('unix/', path, function() end)
s ~= nil
s:close()
fio.stat(path) == nil

{ socket.tcp_connect('abrakadabra#123') == nil, errno.strerror() }


-- wrong options for getaddrinfo
socket.getaddrinfo('host', 'port', { type = 'WRONG' }) == nil and errno() == errno.EINVAL
socket.getaddrinfo('host', 'port', { family = 'WRONG' }) == nil and errno() == errno.EINVAL
socket.getaddrinfo('host', 'port', { protocol = 'WRONG' }) == nil and errno() == errno.EPROTOTYPE
socket.getaddrinfo('host', 'port', { flags = 'WRONG' }) == nil and errno() == errno.EINVAL

-- gh-4634: verify socket path length in socket.tcp_server.
long_port = string.rep('a', 110)
socket.tcp_server('unix/', long_port, function(s) end) == nil and errno() == errno.ENOBUFS

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

-- gh-3168: a stopped tcp server removes the unix socket created
-- and used by a newly started one.
test_run:cmd("setopt delimiter ';'")
err = nil;
path = 'tarantool-test-socket'
for i = 1, 10 do
    local server = socket.tcp_server('unix/', path, function() end)
    if not server then
        err = 'tcp_server: ' .. errno.strerror()
        break
    end
    local client = socket.tcp_connect("unix/", path)
    if not client then
        err = 'tcp_connect: ' .. errno.strerror()
        break
    end
    client:close()
    server:close()
end;
err == nil or err;
test_run:cmd("setopt delimiter ''");


--------------------------------------------------------------------------------
-- Lua Socket Emulation
--------------------------------------------------------------------------------

test_run:cmd("push filter 'fd=([0-9]+)' to 'fd=<FD>'")

s = socket.tcp()
s
s:close()
-- Sic: incompatible with Lua Socket
s:close()

s = socket.tcp()
host, port, family = s:getsockname()
host == '0.0.0.0', port == '0', family == 'inet'
status, reason = s:getpeername()
status == nil, type(reason) == 'string'
s:settimeout(100500)
s:setoption('keepalive', true)
s:setoption('linger', { on = true })
s:setoption('linger', true)
s:setoption('reuseaddr', true)
s:setoption('tcp-nodelay', true)
s:setoption('unknown', true)
s:bind('127.0.0.1', 0)
s:bind('127.0.0.1', 0) -- error handling
s:listen(10)
s -- transformed to tcp{server} socket
host, port, family = s:getsockname()
host == '127.0.0.1', type(port) == 'string', family == 'inet'
status, reason = s:getpeername()
status == nil, type(reason) == 'string'
s:settimeout(0)
status, reason = s:accept()
status == nil, type(reason) == 'string'
s:settimeout(0.001)
status, reason = s:accept()
status == nil, type(reason) == 'string'
s:settimeout(100500)

rch, wch = fiber.channel(1), fiber.channel(1)
sc = socket.connect(host, port)
test_run:cmd("setopt delimiter ';'")
cfiber = fiber.create(function(sc, rch, wch)
    while true do
        local outgoing_data = wch:get()
        if outgoing_data == nil then
            sc:close()
            break
        end
        sc:send(outgoing_data)
        local incoming_data = sc:receive("*l")
        rch:put(incoming_data)
    end
end, sc, rch, wch);
test_run:cmd("setopt delimiter ''");

c = s:accept()
c
chost, cport, cfamily = c:getsockname()
chost == '127.0.0.1', type(cport) == 'string', cfamily == 'inet'
chost, cport, cfamily = c:getpeername()
chost == '127.0.0.1', type(cport) == 'string', cfamily == 'inet'

wch:put("Ping\n")
c:receive("*l")
c:send("Pong\n")
rch:get()
wch:put("HELO lua\nMAIL FROM: <roman@tarantool.org>\n")
c:receive("*l")
c:receive("*l")
c:send("250 Welcome to Lua Universe\n")
c:send("$$$250 OK\n$$$", 4, 11)
rch:get()
wch:put("RCPT TO: <team@lua.org>\n")
c:receive()
c:send("250")
c:send(" ")
c:send("OK")
c:send("\n")
rch:get()
wch:put("DATA\n")
c:receive(4)
c:receive("*l")
rch:get()
wch:put("Fu")
c:send("354 Please type your message\n")
wch:put(nil) -- EOF
c:receive("*l", "Line: ")
c:receive()
c:receive(10)
c:receive("*a")
c:close()

-- eof with bytes
sc = socket.connect(host, port)
sc
c = s:accept()
c
_ = fiber.create(function() sc:send("Po") end)
sc:close()
c:receive(100500, "Message:")
c:close()

-- eof with '*l'
sc = socket.connect(host, port)
sc
c = s:accept()
c
_ = fiber.create(function() sc:send("Pong\nPo") end)
sc:close()
c:receive("*l", "Message:")
c:receive("*l", "Message: ")
c:receive("*l", "Message: ")
c:close()

-- eof with '*a'
sc = socket.connect(host, port)
sc
c = s:accept()
c
_ = fiber.create(function() sc:send("Pong\n") end)
sc:close()
c:receive("*a", "Message: ")
c:receive("*a", "Message: ")
c:close()

-- shutdown
sc = socket.connect(host, port)
sc
c = s:accept()
c
_ = fiber.create(function() sc:send("Pong\n") end)
sc:shutdown("send")
c:receive()
c:shutdown("send")
status, reason = c:shutdown("recv")
status == nil, type(reason) == 'string'
status, reason = c:shutdown("recv")
status == nil, type(reason) == 'string'
status, reason = c:shutdown("both")
status == nil, type(reason) == 'string'
c:close()
sc:close()
s:close()

-- socket.bind / socket.connect
s = socket.bind('127.0.0.1', 0)
s
host, port, family = s:getsockname()
sc = socket.connect(host, port)
sc
sc:close()
sc = socket.tcp()
sc:connect(host, port)
sc:close()
s:close()

--------------------------------------------------------------------------------
-- gh-3619: socket.recvfrom crops UDP packets
--------------------------------------------------------------------------------

test_run:cmd("setopt delimiter ';'")
function sendto_zero(self, host, port)
    local fd = self:fd()

    host = tostring(host)
    port = tostring(port)

    local addrbuf = ffi.new('char[128]') --[[ enough to fit any address ]]
    local addr = ffi.cast('struct sockaddr *', addrbuf)
    local addr_len = ffi.new('socklen_t[1]')
    addr_len[0] = ffi.sizeof(addrbuf)

    self._errno = nil
    local res = ffi.C.lbox_socket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.sendto(fd, nil, 0, 0, addr, addr_len[0])
    end

    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    return tonumber(res)
end;
test_run:cmd("setopt delimiter ''");

receiving_socket = socket('AF_INET', 'SOCK_DGRAM', 'udp')
receiving_socket:bind('127.0.0.1', 0)
receiving_socket_port = receiving_socket:name().port
sending_socket = socket('AF_INET', 'SOCK_DGRAM', 'udp')

-- case: recv, no datagram
received_message = receiving_socket:recv()
e = receiving_socket:errno()
received_message == nil -- expected true
received_message
e == errno.EAGAIN -- expected true

-- case: recvfrom, no datagram
received_message, from = receiving_socket:recvfrom()
e = receiving_socket:errno()
received_message == nil -- expected true
received_message
from == nil -- expected true
from
e == errno.EAGAIN -- expected true

-- case: recv, zero datagram
sendto_zero(sending_socket, '127.0.0.1', receiving_socket_port)
receiving_socket:readable(TIMEOUT)
received_message = receiving_socket:recv()
e = receiving_socket:errno()
received_message == '' -- expected true
received_message
e == 0 -- expected true

-- case: recvfrom, zero datagram
sendto_zero(sending_socket, '127.0.0.1', receiving_socket_port)
receiving_socket:readable(TIMEOUT)
received_message, from = receiving_socket:recvfrom()
e = receiving_socket:errno()
received_message == '' -- expected true
received_message
from ~= nil -- expected true
from.host == '127.0.0.1' -- expected true
e == 0 -- expected true

-- case: recv, no datagram, explicit size
received_message = receiving_socket:recv(512)
e = receiving_socket:errno()
received_message == nil -- expected true
received_message
e == errno.EAGAIN -- expected true

-- case: recvfrom, no datagram, explicit size
received_message, from = receiving_socket:recvfrom(512)
e = receiving_socket:errno()
received_message == nil -- expected true
received_message
from == nil -- expected true
from
e == errno.EAGAIN -- expected true

-- case: recv, zero datagram, explicit size
sendto_zero(sending_socket, '127.0.0.1', receiving_socket_port)
receiving_socket:readable(TIMEOUT)
received_message = receiving_socket:recv(512)
e = receiving_socket:errno()
received_message == '' -- expected true
received_message
e == 0 -- expected true

-- case: recvfrom, zero datagram, explicit size
sendto_zero(sending_socket, '127.0.0.1', receiving_socket_port)
receiving_socket:readable(TIMEOUT)
received_message, from = receiving_socket:recvfrom(512)
e = receiving_socket:errno()
received_message == '' -- expected true
received_message
from ~= nil -- expected true
from.host == '127.0.0.1' -- expected true
e == 0 -- expected true

message_len = 1025
message = string.rep('x', message_len)

-- case: recv, non-zero length datagram, the buffer size should be evaluated
sending_socket:sendto('127.0.0.1', receiving_socket_port, message)
receiving_socket:readable(TIMEOUT)
received_message = receiving_socket:recv()
e = receiving_socket:errno()
received_message == message -- expected true
received_message:len()
received_message
e == 0 -- expected true
e

-- case: recvfrom, non-zero length datagram, the buffer size should be
-- evaluated
sending_socket:sendto('127.0.0.1', receiving_socket_port, message)
receiving_socket:readable(TIMEOUT)
received_message, from = receiving_socket:recvfrom()
e = receiving_socket:errno()
received_message == message -- expected true
received_message:len()
received_message
from ~= nil -- expected true
from.host == '127.0.0.1' -- expected true
e == 0 -- expected true
e

-- case: recv truncates a datagram larger then the buffer of an explicit size
sending_socket:sendto('127.0.0.1', receiving_socket_port, message)
receiving_socket:readable(TIMEOUT)
received_message = receiving_socket:recv(512)
e = receiving_socket:errno()
received_message == message:sub(1, 512) -- expected true
received_message:len() == 512 -- expected true
received_message
received_message:len()
e == 0 -- expected true
e

-- we set the different message to ensure the tail of the previous datagram was
-- discarded
message_len = 1025
message = string.rep('y', message_len)

-- case: recvfrom truncates a datagram larger then the buffer of an explicit size
sending_socket:sendto('127.0.0.1', receiving_socket_port, message)
receiving_socket:readable(TIMEOUT)
received_message, from = receiving_socket:recvfrom(512)
e = receiving_socket:errno()
received_message == message:sub(1, 512) -- expected true
received_message:len() == 512 -- expected true
received_message
received_message:len()
from ~= nil -- expected true
from.host == '127.0.0.1' -- expected true
e == 0 -- expected true
e

receiving_socket:close()
sending_socket:close()

message_len = 513
message = string.rep('x', message_len)

listening_socket = socket('AF_INET', 'SOCK_STREAM', 'tcp')
listening_socket:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', true)
listening_socket:bind('127.0.0.1', 0)
listening_socket:listen()
listening_socket_port = listening_socket:name().port
sending_socket = socket('AF_INET', 'SOCK_STREAM', 'tcp')
sending_socket:sysconnect('127.0.0.1', listening_socket_port) or errno() == errno.EINPROGRESS
listening_socket:readable(TIMEOUT)
receiving_socket = listening_socket:accept()
sending_socket:write(message)

-- case: recvfrom reads first 512 bytes from the message with tcp
receiving_socket:readable(TIMEOUT)
received_message, from = receiving_socket:recvfrom()
e = receiving_socket:errno()
received_message == message:sub(1, 512) -- expected true
received_message:len() == 512 -- expected true
received_message
received_message:len()
from == nil -- expected true
from
e == 0 -- expected true
e

-- case: recvfrom does not discard the message tail with tcp
received_message, from = receiving_socket:recvfrom()
e = receiving_socket:errno()
received_message == message:sub(513, 513) -- expected true
received_message:len() == 1 -- expected true
received_message
received_message:len()
from == nil -- expected true
from
e == 0 -- expected true
e

receiving_socket:close()
sending_socket:close()
listening_socket:close()

--gh-3344 connection should not fail is there is a spurious wakeup for io fiber
test_run:cmd("setopt delimiter ';'")
echo_fiber = nil
channel = fiber.channel()
server = socket.tcp_server('127.0.0.1', 0, { handler = function(s)
    echo_fiber = fiber.self()
    channel:put(true)
    while true do
        local b = s:read(1, 0.1)
        if b ~= nil then
            s:write(b)
        end
    end
end, name = 'echoserv'});
test_run:cmd("setopt delimiter ''");
addr = server:name()
client = socket.tcp_connect(addr.host, addr.port)
channel:get()
echo_fiber ~= nil
client:write('hello')
client:read(5, TIMEOUT) == 'hello'
-- send spurious wakeup
fiber.wakeup(echo_fiber)
fiber.sleep(0)
client:write('world')
client:read(5, TIMEOUT) == 'world'
-- cancel fiber
fiber.cancel(echo_fiber)
client:read(1, TIMEOUT) == ''
server:close()

test_run:cmd("clear filter")

-- case: socket receive inconsistent behavior
chan = fiber.channel()
counter = 0
fn = function(s) counter = 0; while true do s:write((tostring(counter)):rep(chan:get())); counter = counter + 1 end end
srv = socket.tcp_server('127.0.0.1', 0, fn)
s = socket.connect('127.0.0.1', srv:name().port)
chan:put(5)
chan:put(5)
s:receive(5)
chan:put(5)
s:settimeout(1)
s:receive('*a')
s:close()
srv:close()
--
-- gh-4087: fix error while closing socket.tcp_server
--
srv = socket.tcp_server('127.0.0.1', 0, function() end)
srv:close()
test_run:wait_log('default', 'stopped', 1024, 0.01)
