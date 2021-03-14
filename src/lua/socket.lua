-- socket.lua (internal file)

local TIMEOUT_INFINITY      = 500 * 365 * 86400
local LIMIT_INFINITY = 2147483647

local ffi = require('ffi')
local boxerrno = require('errno')
local internal = require('socket')
local fiber = require('fiber')
local fio = require('fio')
local log = require('log')
local buffer = require('buffer')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put
local cord_ibuf_drop = buffer.internal.cord_ibuf_drop

local format = string.format

ffi.cdef[[
    struct gc_socket {
        const int fd;
    };
    typedef uint32_t socklen_t;
    typedef ptrdiff_t ssize_t;

    int connect(int sockfd, const struct sockaddr *addr,
                socklen_t addrlen);
    int bind(int sockfd, const struct sockaddr *addr,
                socklen_t addrlen);

    ssize_t write(int fd, const char *octets, size_t len);
    ssize_t read(int fd, void *buf, size_t count);
    int listen(int fd, int backlog);
    int socket(int domain, int type, int protocol);
    int coio_close(int s);
    int shutdown(int s, int how);
    ssize_t send(int sockfd, const void *buf, size_t len, int flags);
    ssize_t recv(int s, void *buf, size_t len, int flags);
    int accept(int s, void *addr, void *addrlen);
    ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                   const struct sockaddr *dest_addr, socklen_t addrlen);

    int
    lbox_socket_local_resolve(const char *host, const char *port,
                         struct sockaddr *addr, socklen_t *socklen);
    int lbox_socket_nonblock(int fd, int mode);

    int setsockopt(int s, int level, int iname, const void *opt, size_t optlen);
    int getsockopt(int s, int level, int iname, void *ptr, size_t *optlen);

    typedef struct { int active; int timeout; } linger_t;

    struct protoent {
        char  *p_name;       /* official protocol name */
        char **p_aliases;    /* alias list */
        int    p_proto;      /* protocol number */
    };
    struct protoent *getprotobyname(const char *name);

    void *memmem(const void *haystack, size_t haystacklen,
        const void *needle, size_t needlelen);
]]

local gc_socket_t = ffi.metatype(ffi.typeof('struct gc_socket'), {
    __gc = function (socket)
        if socket.fd < 0 then return end
        if ffi.C.coio_close(socket.fd) ~= 0 then
            log.error("socket: failed to close fd=%d on gc: %s", socket.fd,
                boxerrno.strerror())
        end
    end
})

local socket_mt

local function socket_is_closed(socket)
    -- The type of 'socket' is not checked because the function
    -- is internal and the type must be checked outside if needed.
    return socket._gc_socket.fd < 0
end

local function check_socket(socket)
    local gc_socket = type(socket) == 'table' and socket._gc_socket
    if ffi.istype(gc_socket_t, gc_socket) then
        if not socket_is_closed(socket) then
            return gc_socket.fd
        else
            error("attempt to use closed socket")
        end
    else
        local msg = "Usage: socket:method()"
        if socket ~= nil then msg = msg .. ", called with non-socket" end
        error(msg)
    end
end

local function make_socket(fd, itype)
    assert(itype ~= nil)
    local socket = {
        _gc_socket = ffi.new(gc_socket_t, { fd = fd }),
        itype = itype,
    }
    return setmetatable(socket, socket_mt)
end

local gc_socket_sentinel = ffi.new(gc_socket_t, { fd = -1 })

local function socket_close(socket)
    local fd = check_socket(socket)
    socket._errno = nil
    local r = ffi.C.coio_close(fd)
    -- .fd is const to prevent tampering
    ffi.copy(socket._gc_socket, gc_socket_sentinel, ffi.sizeof(gc_socket_t))
    if r ~= 0 then
        socket._errno = boxerrno()
        return false
    end
    return true
end

local soname_mt = {
    __tostring = function(si)
        if si.host == nil and si.port == nil then
            return ''
        end
        if si.host == nil then
            return format('%s:%s', '0', tostring(si.port))
        end

        if si.port == nil then
            return format('%s:%', tostring(si.host), 0)
        end
        return format('%s:%s', tostring(si.host), tostring(si.port))
    end
}

local function socket_name(self)
    local fd = check_socket(self)
    local aka = internal.name(fd)
    if aka == nil then
        self._errno = boxerrno()
        return nil
    end
    self._errno = nil
    setmetatable(aka, soname_mt)
    return aka
end

local function socket_peer(self)
    local fd = check_socket(self)
    local peer = internal.peer(fd)
    if peer == nil then
        self._errno = boxerrno()
        return nil
    end
    self._errno = nil
    setmetatable(peer, soname_mt)
    return peer
end

local function socket_fd(self)
    return check_socket(self)
end

local function get_ivalue(table, key)
    if type(key) == 'number' then
        return key
    end
    return table[key]
end

local function get_iflags(table, flags)
    if flags == nil then
        return 0
    end
    local res = 0
    if type(flags) ~= 'table' then
        flags = { flags }
    end
    for _, f in pairs(flags) do
        if table[f] == nil then
            return nil
        end
        res = bit.bor(res, table[f])
    end
    return res
end

local function getprotobyname(name)
    if type(name) == 'number' then
        return name
    elseif type(name) ~= 'string' then
        boxerrno(boxerrno.EINVAL)
        return nil
    end
    local num = internal.protocols[name]
    if num ~= nil then
        return num
    end
    local p = ffi.C.getprotobyname(name)
    if p == nil then
        boxerrno(boxerrno.EPROTOTYPE)
        return nil
    end
    num = p.p_proto
    -- update cache
    internal.protocols[name] = num
    return num
end

local function socket_errno(self)
    check_socket(self)
    if self['_errno'] == nil then
        return 0
    else
        return self['_errno']
    end
end

local function socket_error(self)
    check_socket(self)
    if self['_errno'] == nil then
        return nil
    else
        return boxerrno.strerror(self._errno)
    end
end

-- addrbuf is equivalent to struct sockaddr_storage
local addrbuf = ffi.new('char[128]') -- enough to fit any address
local addr = ffi.cast('struct sockaddr *', addrbuf)
local addr_len = ffi.new('socklen_t[1]')
local function socket_sysconnect(self, host, port)
    local fd = check_socket(self)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    addr_len[0] = ffi.sizeof(addrbuf)
    local res = ffi.C.lbox_socket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.connect(fd, addr, addr_len[0]);
        if res == 0 then
            return true
        end
    end
    self._errno = boxerrno()
    return false
end

local function syswrite(self, charptr, size)
    local fd = check_socket(self)
    self._errno = nil
    local done = ffi.C.write(fd, charptr, size)
    if done < 0 then
        self._errno = boxerrno()
        return nil
    end

    return tonumber(done)
end

local function socket_syswrite(self, arg1, arg2)
    -- TODO: ffi.istype('char *', arg1) doesn't work for ffi.new('char[256]')
    if type(arg1) == 'cdata' and arg2 ~= nil then
        return syswrite(self, arg1, arg2)
    elseif type(arg1) == 'string' then
        return syswrite(self, arg1, #arg1)
    else
        error('Usage: socket:syswrite(data) or socket:syswrite(const char *, size)')
    end
end

local function sysread(self, charptr, size)
    local fd = check_socket(self)

    self._errno = nil
    local res = ffi.C.read(fd, charptr, size)
    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    return tonumber(res)
end

local function socket_sysread(self, arg1, arg2)
    -- TODO: ffi.istype('char *', arg1) doesn't work for ffi.new('char[256]')
    if type(arg1) == 'cdata' and arg2 ~= nil then
        return sysread(self, arg1, arg2)
    end

    local size = arg1 or buffer.READAHEAD
    if size < 0 then
        error('socket:sysread(): size can not be negative')
    end

    local buf = cord_ibuf_take()
    local p = buf:alloc(size)

    local res = sysread(self, p, size)
    if res then
        res = ffi.string(p, res)
    end
    cord_ibuf_drop(buf)
    return res
end

local function socket_nonblock(self, nb)
    local fd = check_socket(self)
    self._errno = nil

    local res

    if nb == nil then
        res = ffi.C.lbox_socket_nonblock(fd, 0x80)
    elseif nb then
        res = ffi.C.lbox_socket_nonblock(fd, 1)
    else
        res = ffi.C.lbox_socket_nonblock(fd, 0)
    end

    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    if res == 1 then
        return true
    else
        return false
    end
end

local function do_wait(self, what, timeout)
    local fd = check_socket(self)

    self._errno = nil
    timeout = timeout or TIMEOUT_INFINITY

    repeat
        local started = fiber.clock()
        local events = internal.iowait(fd, what, timeout)
        fiber.testcancel()
        if events ~= 0 and events ~= '' then
            return events
        end
        timeout = timeout - (fiber.clock() - started)
    until timeout < 0
    self._errno = boxerrno.ETIMEDOUT
    return 0
end

local function socket_readable(self, timeout)
    return do_wait(self, 1, timeout) ~= 0
end

local function socket_writable(self, timeout)
    return do_wait(self, 2, timeout) ~= 0
end

local function socket_wait(self, timeout)
    return do_wait(self, 'RW', timeout)
end

local function socket_listen(self, backlog)
    local fd = check_socket(self)
    self._errno = nil
    if backlog == nil then
        backlog = 256
    end
    local res = ffi.C.listen(fd, backlog)
    if res < 0 then
        self._errno = boxerrno()
        return false
    end
    return true
end

local function socket_bind(self, host, port)
    local fd = check_socket(self)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    addr_len[0] = ffi.sizeof(addrbuf)
    local res = ffi.C.lbox_socket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.bind(fd, addr, addr_len[0]);
    end
    if res == 0 then
        return true
    end

    self._errno = boxerrno()
    return false
end

local function socket_shutdown(self, how)
    local fd = check_socket(self)
    local hvariants = {
        ['R']           = 0,
        ['READ']        = 0,
        ['receive']     = 0,
        ['W']           = 1,
        ['WRITE']       = 1,
        ['send']        = 1,
        ['RW']          = 2,
        ['READ_WRITE']  = 2,
        ["both"]        = 2,

        [0]             = 0,
        [1]             = 1,
        [2]             = 2
    }
    local ihow = hvariants[how]

    if ihow == nil then
        ihow = 2
    end
    self._errno = nil
    if ffi.C.shutdown(fd, ihow) < 0 then
        self._errno = boxerrno()
        return false
    end
    return true
end

local function getsol(level)
    if type(level) == 'number' then
        return level
    elseif type(level) ~= 'string' then
        boxerrno(boxerrno.EINVAL)
        return nil
    elseif level == 'SOL_SOCKET' or level == 'socket' then
        return internal.SOL_SOCKET
    end
    level = (level:match('IPPROTO_([A-Z]*)') or
             level:match('SOL_([A-Z]*)') or
             level):lower()
    level = getprotobyname(level)
    if level == nil then
        return nil
    end
    return level
end

local function socket_setsockopt(self, level, name, value)
    local fd = check_socket(self)

    level = getsol(level)
    if level == nil then
        self._errno = boxerrno()
        return false
    end

    local info = get_ivalue(internal.SO_OPT[level] or {}, name)
    if info == nil then
        error(format("Unknown socket option name: %s", tostring(name)))
    end

    if not info.rw then
        error(format("Socket option %s is read only", name))
    end

    self._errno = nil

    if type(value) == 'boolean' then
        if value then
            value = 1
        else
            value = 0
        end
    end

    if info.type == 1 then
        local ai = ffi.new('int[1]', value)
        local res = ffi.C.setsockopt(fd,
            level, info.iname, ai, ffi.sizeof('int'))

        if res < 0 then
            self._errno = boxerrno()
            return false
        end
        return true
    end

    if info.type == 2 then
        local res = ffi.C.setsockopt(fd,
            level, info.iname, value, ffi.sizeof('size_t'))
        if res < 0 then
            self._errno = boxerrno()
            return false
        end
        return true
    end

    if name == 'SO_LINGER' then
        error("Use s:linger(active[, timeout])")
    end
    error(format("Unsupported socket option: %s", name))
end

local function socket_getsockopt(self, level, name)
    local fd = check_socket(self)

    level = getsol(level)
    if level == nil then
        self._errno = boxerrno()
        return false
    end

    local info = get_ivalue(internal.SO_OPT[level] or {}, name)
    if info == nil then
        error(format("Unknown socket option name: %s", tostring(name)))
    end

    self._errno = nil

    if info.type == 1 then
        local value = ffi.new('int[1]')
        local len = ffi.new('size_t[1]', ffi.sizeof('int'))
        local res = ffi.C.getsockopt(fd, level, info.iname, value, len)

        if res < 0 then
            self._errno = boxerrno()
            return nil
        end

        if len[0] ~= 4 then
            error(format("Internal error: unexpected optlen: %d", len[0]))
        end
        return tonumber(value[0])
    end

    if info.type == 2 then
        local ibuf = cord_ibuf_take()
        local value = ibuf:alloc(256)
        local len = ffi.new('size_t[1]', 256)
        local res = ffi.C.getsockopt(fd, level, info.iname, value, len)
        if res < 0 then
            self._errno = boxerrno()
            cord_ibuf_put(ibuf)
            return nil
        end
        value = ffi.string(value, tonumber(len[0]))
        cord_ibuf_put(ibuf)
        return value
    end

    if name == 'SO_LINGER' then
        error("Use s:linger()")
    end
    error(format("Unsupported socket option: %s", name))
end

local function socket_linger(self, active, timeout)
    local fd = check_socket(self)

    local level = internal.SOL_SOCKET
    local info = internal.SO_OPT[level].SO_LINGER
    self._errno = nil
    if active == nil then
        local value = ffi.new('linger_t[1]')
        local len = ffi.new('size_t[1]', ffi.sizeof('linger_t'))
        local res = ffi.C.getsockopt(fd, level, info.iname, value, len)
        if res < 0 then
            self._errno = boxerrno()
            return nil
        end
        if value[0].active ~= 0 then
            active = true
        else
            active = false
        end
        return active, value[0].timeout
    end

    if timeout == nil then
        timeout = 0
    end

    local iactive
    if active then
        iactive = 1
    else
        iactive = 0
    end

    local value = ffi.new("linger_t[1]",
        { { active = iactive, timeout = timeout } })
    local len = 2 * ffi.sizeof('int')
    local res = ffi.C.setsockopt(fd, level, info.iname, value, len)
    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    return active, timeout
end

local function socket_accept(self)
    local server_fd = check_socket(self)
    self._errno = nil

    local client_fd, from = internal.accept(server_fd)
    if client_fd == nil then
        self._errno = boxerrno()
        return nil
    end
    local client = make_socket(client_fd, self.itype)
    if not client:nonblock(true) then
        client:close()
        return
    end
    return client, from
end

local errno_is_transient = {
    [boxerrno.EAGAIN] = true;
    [boxerrno.EWOULDBLOCK] = true;
    [boxerrno.EINTR] = true;
}

local errno_is_fatal = {
    [boxerrno.EBADF] = true;
    [boxerrno.EINVAL] = true;
    [boxerrno.EOPNOTSUPP] = true;
    [boxerrno.ENOTSOCK] = true;
}

local function check_limit(self, limit)
    if self.rbuf:size() >= limit then
        return limit
    end
    return nil
end

local function check_infinity(self, limit)
    if limit == 0 then
        return 0
    end

    local rbuf = self.rbuf
    if rbuf:size() == 0 then
        return nil
    end

    return rbuf:size()
end

local function check_delimiter(self, limit, eols)
    if limit == 0 then
        return 0
    end
    local rbuf = self.rbuf
    if rbuf:size() == 0 then
        return nil
    end

    local shortest
    for _, eol in ipairs(eols) do
        local data = ffi.C.memmem(rbuf.rpos, rbuf:size(), eol, #eol)
        if data ~= nil then
            local len = ffi.cast('char *', data) - rbuf.rpos + #eol
            if shortest == nil or shortest > len then
                shortest = len
            end
        end
    end
    if shortest ~= nil and shortest <= limit then
        return shortest
    elseif limit <= rbuf:size() then
        return limit
    end
    return nil
end

local function read(self, limit, timeout, check, ...)
    if limit < 0 then
        error('socket:read(): limit can not be negative')
    end

    limit = math.min(limit, LIMIT_INFINITY)
    local rbuf = self.rbuf
    if rbuf == nil then
        rbuf = buffer.ibuf()
        self.rbuf = rbuf
    end

    local len = check(self, limit, ...)
    if len ~= nil then
        self._errno = nil
        local data = ffi.string(rbuf.rpos, len)
        rbuf.rpos = rbuf.rpos + len
        return data
    end

    local deadline = fiber.clock() + timeout
    repeat
        assert(rbuf:size() < limit)
        local to_read = math.min(limit - rbuf:size(), buffer.READAHEAD)
        local data = rbuf:reserve(to_read)
        assert(rbuf:unused() >= to_read)
        local res = sysread(self, data, rbuf:unused())
        if res == 0 then -- eof
            self._errno = nil
            local len = rbuf:size()
            local data = ffi.string(rbuf.rpos, len)
            rbuf.rpos = rbuf.rpos + len
            return data
        elseif res ~= nil then
            rbuf.wpos = rbuf.wpos + res
            local len = check(self, limit, ...)
            if len ~= nil then
                self._errno = nil
                local data = ffi.string(rbuf.rpos, len)
                rbuf.rpos = rbuf.rpos + len
                return data
            end
        elseif not errno_is_transient[self._errno] then
            return nil
        end
    until not socket_readable(self, deadline - fiber.clock())
    return nil
end

local function socket_read(self, opts, timeout)
    check_socket(self)
    timeout = timeout or TIMEOUT_INFINITY
    if type(opts) == 'number' then
        return read(self, opts, timeout, check_limit)
    elseif type(opts) == 'string' then
        return read(self, LIMIT_INFINITY, timeout, check_delimiter, { opts })
    elseif type(opts) == 'table' then
        local chunk = opts.chunk or opts.size or LIMIT_INFINITY
        local delimiter = opts.delimiter or opts.line
        if delimiter == nil then
            return read(self, chunk, timeout, check_limit)
        elseif type(delimiter) == 'string' then
            return read(self, chunk, timeout, check_delimiter, { delimiter })
        elseif type(delimiter) == 'table' then
            return read(self, chunk, timeout, check_delimiter, delimiter)
        end
    end
    error('Usage: s:read(delimiter|chunk|{delimiter = x, chunk = x}, timeout)')
end

local function socket_write(self, octets, timeout)
    check_socket(self)
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local s = ffi.cast('const char *', octets)
    local p = s
    local e = s + #octets
    if p == e then
        return 0
    end

    local deadline = fiber.clock() + timeout
    repeat
        local written = syswrite(self, p, e - p)
        if written == 0 then
            return p - s -- eof
        elseif written ~= nil then
            p = p + written
            assert(p <= e)
            if p == e then
                return e - s
            end
        elseif not errno_is_transient[self._errno] then
            return nil
        end

    until not socket_writable(self, deadline - fiber.clock())
    return nil
end

local function socket_send(self, octets, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)

    self._errno = nil
    local res = ffi.C.send(fd, octets, string.len(octets), iflags)
    if res < 0 then
        self._errno = boxerrno()
        return nil
    end
    return tonumber(res)
end

-- Returns nil and sets EAGAIN when tries to determine the next
-- datagram length and there are no datagrams in the receive
-- queue.
local function get_recv_size(self, size)
    if size ~= nil then
        return size
    end

    if self.itype ~= get_ivalue(internal.SO_TYPE, 'SOCK_DGRAM') then
        return 512
    end

    -- Determine the next datagram length.
    local fd = check_socket(self)
    self._errno = nil
    if jit.os == 'OSX' then
        size = self:getsockopt('SOL_SOCKET', 'SO_NREAD')
        -- recv() with zero length buffer is always successful on
        -- Mac OS (at least for valid UDP sockets), so we need
        -- extra calls below to distinguish a zero length datagram
        -- from no datagram situation to set EAGAIN correctly.
        if size == 0 then
            -- No datagram or zero length datagram: distinguish
            -- them using message peek.
            local iflags = get_iflags(internal.SEND_FLAGS, {'MSG_PEEK'})
            assert(iflags ~= nil)
            size = tonumber(ffi.C.recv(fd, ffi.new('char[1]'), 1, iflags))
            -- Prevent race condition: proceed with the case when
            -- a datagram of length > 0 has been arrived after the
            -- getsockopt call above.
            if size > 0 then
                size = self:getsockopt('SOL_SOCKET', 'SO_NREAD')
                assert(size > 0)
            end
        end
    else
        local iflags = get_iflags(internal.SEND_FLAGS, {'MSG_TRUNC', 'MSG_PEEK'})
        assert(iflags ~= nil)
        size = tonumber(ffi.C.recv(fd, nil, 0, iflags))
    end

    if size == -1 then
        self._errno = boxerrno()
        return nil
    end

    return size
end

local function socket_recv(self, size, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end

    size = get_recv_size(self, size)
    if size == nil then
        return nil
    end

    self._errno = nil
    local ibuf = cord_ibuf_take()
    local buf = ibuf:alloc(size)
    local res = ffi.C.recv(fd, buf, size, iflags)

    if res == -1 then
        self._errno = boxerrno()
        cord_ibuf_put(ibuf)
        return nil
    end
    buf = ffi.string(buf, res)
    cord_ibuf_put(ibuf)
    return buf
end

local function socket_recvfrom(self, size, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end

    size = get_recv_size(self, size)
    if size == nil then
        return nil
    end

    self._errno = nil
    local res, from = internal.recvfrom(fd, size, iflags)
    if res == nil then
        self._errno = boxerrno()
        return nil
    end
    return res, from
end

local function socket_sendto(self, host, port, octets, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)

    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end

    self._errno = nil
    if octets == nil or octets == '' then
        return true
    end

    host = tostring(host)
    port = tostring(port)
    octets = tostring(octets)

    addr_len[0] = ffi.sizeof(addrbuf)
    local res = ffi.C.lbox_socket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.sendto(fd, octets, string.len(octets), iflags,
            addr, addr_len[0])
    end
    if res < 0 then
        self._errno = boxerrno()
        return nil
    end
    return tonumber(res)
end

local function socket_new(domain, stype, proto)
    local idomain = get_ivalue(internal.DOMAIN, domain)
    if idomain == nil then
        boxerrno(boxerrno.EINVAL)
        return nil
    end

    local itype = get_ivalue(internal.SO_TYPE, stype)
    if itype == nil then
        boxerrno(boxerrno.EINVAL)
        return nil
    end

    local iproto = getprotobyname(proto)
    if iproto == nil then
        return nil
    end

    local fd = ffi.C.socket(idomain, itype, iproto)
    if fd >= 0 then
        local socket = make_socket(fd, itype)
        if not socket:nonblock(true) then
            socket:close()
        else
            return socket
        end
    end
end

local function getaddrinfo(host, port, timeout, opts)
    if type(timeout) == 'table' and opts == nil then
        opts = timeout
        timeout = TIMEOUT_INFINITY
    elseif timeout == nil then
        timeout = TIMEOUT_INFINITY
    end
    if port == nil then
        port = 0
    end
    local ga_opts = {}
    if opts ~= nil then
        if opts.type ~= nil then
            local itype = get_ivalue(internal.SO_TYPE, opts.type)
            if itype == nil then
                boxerrno(boxerrno.EINVAL)
                return nil, boxerrno.strerror()
            end
            ga_opts.type = itype
        end

        if opts.family ~= nil then
            local ifamily = get_ivalue(internal.DOMAIN, opts.family)
            if ifamily == nil then
                boxerrno(boxerrno.EINVAL)
                return nil, boxerrno.strerror()
            end
            ga_opts.family = ifamily
        end

        if opts.protocol ~= nil then
            local p = getprotobyname(opts.protocol)
            if p == nil then
                return nil, boxerrno.strerror()
            end
            ga_opts.protocol = p
        end

        if opts.flags ~= nil then
            ga_opts.flags =
                get_iflags(internal.AI_FLAGS, opts.flags)
            if ga_opts.flags == nil then
                boxerrno(boxerrno.EINVAL)
                return nil, boxerrno.strerror()
            end
        end

    end
    return internal.getaddrinfo(host, port, timeout, ga_opts)
end

-- tcp connector
local function socket_tcp_connect(s, address, port, timeout)
    local res = socket_sysconnect(s, address, port)
    if res then
        -- Even through the socket is nonblocking, if the server to which we
        -- are connecting is on the same host, the connect is normally
        -- established immediately when we call connect (Stevens UNP).
        return true
    end
    if s._errno ~= boxerrno.EINPROGRESS then
        return nil
    end
    -- Wait until the connection is established or ultimately fails.
    -- In either condition the socket becomes writable. To tell these
    -- conditions appart SO_ERROR must be consulted (man connect).
    if socket_writable(s, timeout) then
        s._errno = socket_getsockopt(s, 'SOL_SOCKET', 'SO_ERROR')
    end
    if s._errno ~= 0 then
        return nil
    end
    -- Connected
    return true
end

local function tcp_connect(host, port, timeout)
    if host == 'unix/' then
        local s = socket_new('AF_UNIX', 'SOCK_STREAM', 0)
        if not s then
            -- Address family is not supported by the host
            return nil, boxerrno.strerror()
        end
        if not socket_tcp_connect(s, host, port, timeout) then
            local save_errno = s._errno
            s:close()
            boxerrno(save_errno)
            return nil, boxerrno.strerror()
        end
        boxerrno(0)
        return s
    end
    local timeout = timeout or TIMEOUT_INFINITY
    local stop = fiber.clock() + timeout
    local dns, err = getaddrinfo(host, port, timeout, { type = 'SOCK_STREAM',
        protocol = 'tcp' })
    if dns == nil then
        return nil, err
    end
    if #dns == 0 then
        boxerrno(boxerrno.EINVAL)
        return nil, boxerrno.strerror()
    end
    for _, remote in pairs(dns) do
        timeout = stop - fiber.clock()
        if timeout <= 0 then
            boxerrno(boxerrno.ETIMEDOUT)
            return nil, boxerrno.strerror()
        end
        local s = socket_new(remote.family, remote.type, remote.protocol)
        if s then
            if socket_tcp_connect(s, remote.host, remote.port, timeout) then
                boxerrno(0)
                return s
            end
            local save_errno = s:errno()
            s:close()
            boxerrno(save_errno)
        end
    end
    -- errno is set by socket_tcp_connect()
    return nil, boxerrno.strerror()
end

local function tcp_server_handler(server, sc, from)
    fiber.name(format("%s/%s:%s", server.name, from.host, from.port), {truncate = true})
    local status, message = pcall(server.handler, sc, from)
    sc:shutdown()
    sc:close()
    if not status then
        error(message)
    end
end

local function tcp_server_loop(server, s, addr)
    fiber.name(format("%s/%s:%s", server.name, addr.host, addr.port), {truncate = true})
    log.info("started")
    while socket_readable(s) do
        if socket_is_closed(s) then
            break
        end
        local sc, from = socket_accept(s)
        if sc == nil then
            local errno = s._errno
            if not errno_is_transient[errno] then
                log.error('accept(%s) failed: %s', tostring(s),
                          socket_error(s))
            end
            if  errno_is_fatal[errno] then
                break
            end
        else
            fiber.create(tcp_server_handler, server, sc, from)
        end
    end
    -- Socket was closed
    log.info("stopped")
end

local function tcp_server_usage()
    error('Usage: socket.tcp_server(host, port, handler | opts)')
end

local function tcp_server_do_bind(s, addr)
    if socket_bind(s, addr.host, addr.port) then
        if addr.family == 'AF_UNIX' then
            -- Make close() remove the unix socket file created
            -- by bind(). Note, this must be done before closing
            -- the socket fd so that no other tcp server can
            -- reuse the same path before we remove the file.
            s.close = function(self)
                fio.unlink(addr.port)
                return socket_close(self)
            end
        end
        return true
    end
    return false
end

local function tcp_server_bind_addr(s, addr)
    if tcp_server_do_bind(s, addr) then
        return true
    end

    if addr.family ~= 'AF_UNIX' then
        return false
    end

    if boxerrno() ~= boxerrno.EADDRINUSE then
        return false
    end

    local save_errno = boxerrno()

    local sc = tcp_connect(addr.host, addr.port)
    if sc ~= nil then
        sc:close()
        boxerrno(save_errno)
        return false
    end

    if boxerrno() ~= boxerrno.ECONNREFUSED then
        boxerrno(save_errno)
        return false
    end

    log.info("tcp_server: remove dead UNIX socket: %s", addr.port)
    if not fio.unlink(addr.port) then
        log.warn("tcp_server: %s", boxerrno.strerror())
        boxerrno(save_errno)
        return false
    end
    return tcp_server_do_bind(s, addr)
end


local function tcp_server_bind(host, port, prepare, timeout)
    timeout = timeout and tonumber(timeout) or TIMEOUT_INFINITY
    local dns, err
    if host == 'unix/' then
        dns = {{host = host, port = port, family = 'AF_UNIX', protocol = 0,
            type = 'SOCK_STREAM' }}
    else
        dns, err = getaddrinfo(host, port, timeout, { type = 'SOCK_STREAM',
            flags = 'AI_PASSIVE'})
        if dns == nil then
            return nil, err
        end
    end

    for _, addr in ipairs(dns) do
        local s = socket_new(addr.family, addr.type, addr.protocol)
        if s ~= nil then
            local backlog
            if prepare then
                backlog = prepare(s)
            else
                socket_setsockopt(s, 'SOL_SOCKET', 'SO_REUSEADDR', 1) -- ignore error
            end
            if not tcp_server_bind_addr(s, addr) or not s:listen(backlog) then
                local save_errno = boxerrno()
                socket_close(s)
                boxerrno(save_errno)
                return nil, boxerrno.strerror()
            end
            return s, addr
       end
    end
    -- DNS resolved successfully, but addresss family is not supported
    boxerrno(boxerrno.EAFNOSUPPORT)
    return nil, boxerrno.strerror()
end

local function tcp_server(host, port, opts, timeout)
    local server = {}
    if type(opts) == 'function' then
        server.handler = opts
    elseif type(opts) == 'table' then
        if type(opts.handler) ~='function' or (opts.prepare ~= nil and
            type(opts.prepare) ~= 'function') then
            tcp_server_usage()
        end
        for k, v in pairs(opts) do
            server[k] = v
        end
    else
        tcp_server_usage()
    end
    server.name = server.name or 'server'
    local s, addr = tcp_server_bind(host, port, server.prepare, timeout)
    if not s then
        -- addr is error message now.
        return nil, addr
    end
    fiber.create(tcp_server_loop, server, s, addr)
    return s, addr
end

socket_mt   = {
    __index = {
        close = socket_close;
        errno = socket_errno;
        error = socket_error;
        sysconnect = socket_sysconnect;
        syswrite = socket_syswrite;
        sysread = socket_sysread;
        nonblock = socket_nonblock;
        readable = socket_readable;
        writable = socket_writable;
        wait = socket_wait;
        listen = socket_listen;
        bind = socket_bind;
        shutdown = socket_shutdown;
        setsockopt = socket_setsockopt;
        getsockopt = socket_getsockopt;
        linger = socket_linger;
        accept = socket_accept;
        read = socket_read;
        write = socket_write;
        send = socket_send;
        recv = socket_recv;
        recvfrom = socket_recvfrom;
        sendto = socket_sendto;
        name = socket_name;
        peer = socket_peer;
        fd = socket_fd;
    };
    __tostring  = function(self)
        local fd = check_socket(self)

        local save_errno = self._errno
        local name = format("fd %d", fd)
        local aka = socket_name(self)
        if aka ~= nil then
            name = format("%s, aka %s:%s", name, aka.host, aka.port)
        end
        local peer = socket_peer(self)
        if peer ~= nil then
            name = format("%s, peer %s:%s", name, peer.host, peer.port)
        end
        self._errno = save_errno
        return name
    end,
    __serialize = function(self)
        -- Allow YAML, MsgPack and JSON to dump objects with sockets
        local fd = check_socket(self)
        return { fd = fd, peer = socket_peer(self), name = socket_name(self) }
    end
}

--------------------------------------------------------------------------------
-- Lua Socket Emulation
--------------------------------------------------------------------------------

local lsocket_tcp_mt
local lsocket_tcp_server_mt
local lsocket_tcp_client_mt

--
-- TCP Master Socket
--

local function lsocket_tcp_tostring(self)
    local fd = check_socket(self)
    return string.format("tcp{master}: fd=%d", fd)
end

local function lsocket_tcp_close(self)
    if not socket_close(self) then
        return nil, socket_error(self)
    end
    return 1
end

local function lsocket_tcp_getsockname(self)
    local aka = socket_name(self)
    if aka == nil then
        return nil, socket_error(self)
    end
    return aka.host, tostring(aka.port), aka.family:match("AF_(.*)"):lower()
end

local function lsocket_tcp_getpeername(self)
    local peer = socket_peer(self)
    if peer == nil then
        return nil, socket_error(self)
    end
    return peer.host, tostring(peer.port), peer.family:match("AF_(.*)"):lower()
end

local function lsocket_tcp_settimeout(self, value)
    check_socket(self)
    self.timeout = value
    -- mode is effectively ignored
    return 1
end

local function lsocket_tcp_setoption(self, option, value)
    local r
    if option == 'reuseaddr' then
        r = socket_setsockopt(self, 'socket', 'SO_REUSEADDR', value)
    elseif option == 'keepalive' then
        r = socket_setsockopt(self, 'socket', 'SO_KEEPALIVE', value)
    elseif option == 'linger' then
        value = type(value) == 'table' and value.on or value
        -- Sic: value.timeout is ignored
        r = socket_linger(self, value)
    elseif option == 'tcp-nodelay' then
        r = socket_setsockopt(self, 'tcp', 'TCP_NODELAY', value)
    else
        error(format("Unknown socket option name: %s", tostring(option)))
    end
    if not r then
        return nil, socket_error(self)
    end
    return 1
end

local function lsocket_tcp_bind(self, address, port)
    if not socket_bind(self, address, port) then
        return nil, socket_error(self)
    end
    return 1
end

local function lsocket_tcp_listen(self, backlog)
    if not socket_listen(self, backlog) then
        return nil, socket_error(self)
    end
    setmetatable(self, lsocket_tcp_server_mt)
    return 1
end

local function lsocket_tcp_connect(self, host, port)
    check_socket(self)
    local deadline = fiber.clock() + (self.timeout or TIMEOUT_INFINITY)
    -- This function is broken by design
    local ga_opts = { family = 'AF_INET', type = 'SOCK_STREAM' }
    local timeout = deadline - fiber.clock()
    local dns, err = getaddrinfo(host, port, timeout, ga_opts)
    if dns == nil then
        self._errno = boxerrno.EINVAL
        return nil, err
    end
    if #dns == 0 then
        self._errno = boxerrno.EINVAL
        return nil, socket_error(self)
    end
    for _, remote in ipairs(dns) do
        timeout = deadline - fiber.clock()
        if socket_tcp_connect(self, remote.host, remote.port, timeout) then
            return 1
        end
    end
    return nil, socket_error(self)
end

lsocket_tcp_mt = {
    __index = {
        close = lsocket_tcp_close;
        getsockname = lsocket_tcp_getsockname;
        getpeername = lsocket_tcp_getpeername;
        settimeout = lsocket_tcp_settimeout;
        setoption = lsocket_tcp_setoption;
        bind = lsocket_tcp_bind;
        listen = lsocket_tcp_listen;
        connect = lsocket_tcp_connect;
    };
    __tostring = lsocket_tcp_tostring;
    __serialize = lsocket_tcp_tostring;
};

--
-- TCP Server Socket
--

local function lsocket_tcp_server_tostring(self)
    local fd = check_socket(self)
    return string.format("tcp{server}: fd=%d", fd)
end

local function lsocket_tcp_accept(self)
    check_socket(self)
    local deadline = fiber.clock() + (self.timeout or TIMEOUT_INFINITY)
    repeat
        local client = socket_accept(self)
        if client then
            setmetatable(client, lsocket_tcp_client_mt)
            return client
        end
        local errno = socket_errno(self)
        if not errno_is_transient[errno] then
            break
        end
    until not socket_readable(self, deadline - fiber.clock())
    return nil, socket_error(self)
end

lsocket_tcp_server_mt = {
    __index = {
        close = lsocket_tcp_close;
        getsockname = lsocket_tcp_getsockname;
        getpeername = lsocket_tcp_getpeername;
        settimeout = lsocket_tcp_settimeout;
        setoption = lsocket_tcp_setoption;
        accept = lsocket_tcp_accept;
    };
    __tostring = lsocket_tcp_server_tostring;
    __serialize = lsocket_tcp_server_tostring;
};

--
-- TCP Client Socket
--

local function lsocket_tcp_client_tostring(self)
    local fd = check_socket(self)
    return string.format("tcp{client}: fd=%d", fd)
end

local function lsocket_tcp_receive(self, pattern, prefix)
    check_socket(self)
    prefix = prefix or ''
    local timeout = self.timeout or TIMEOUT_INFINITY
    local data
    if type(pattern) == 'number' then
        data = read(self, pattern, timeout, check_limit)
        if data == nil then
            return nil, socket_error(self)
        elseif #data < pattern then
            -- eof
            return nil, 'closed', prefix..data
        else
            return prefix..data
        end
    elseif pattern == "*l" or pattern == nil then
        data = read(self, LIMIT_INFINITY, timeout, check_delimiter, {"\n"})
        if data == nil then
            return nil, socket_error(self)
        elseif #data > 0 and data:byte(#data) == 10 then
            -- remove '\n'
            return prefix..data:sub(1, #data - 1)
        else
            -- eof
            return nil, 'closed', prefix..data
        end
    elseif pattern == "*a" then
        local result = { prefix }
        local deadline = fiber.clock() + (self.timeout or TIMEOUT_INFINITY)
        repeat
            local data = read(self, LIMIT_INFINITY, timeout, check_infinity)
            if data == nil then
                if not errno_is_transient[self._errno] then
                    return nil, socket_error(self)
                end
            elseif data == '' then
                break
            else
                table.insert(result, data)
            end
        until not socket_readable(self, deadline - fiber.clock())
        if #result == 1 then
            return nil, 'closed', table.concat(result)
        end
        return table.concat(result)
    else
        error("Usage: socket:receive(pattern, [, prefix])")
    end
end

local function lsocket_tcp_send(self, data, i, j)
    if i ~= nil then
        data = string.sub(data, i, j)
    end
    local sent = socket_write(self, data, self.timeout)
    if not sent then
        return nil, socket_error(self)
    end
    return (i or 1) + sent - 1
end

local function lsocket_tcp_shutdown(self, how)
    if not socket_shutdown(self, how) then
        return nil, socket_error(self)
    end
    return 1
end

lsocket_tcp_client_mt = {
    __index = {
        close = lsocket_tcp_close;
        getsockname = lsocket_tcp_getsockname;
        getpeername = lsocket_tcp_getpeername;
        settimeout = lsocket_tcp_settimeout;
        setoption = lsocket_tcp_setoption;
        receive = lsocket_tcp_receive;
        send = lsocket_tcp_send;
        shutdown = lsocket_tcp_shutdown;
    };
    __tostring = lsocket_tcp_client_tostring;
    __serialize = lsocket_tcp_client_tostring;
};

--
-- Unconnected tcp socket (tcp{master}) should not have receive() and
-- send methods according to LuaSocket documentation[1]. Unfortunally,
-- original implementation is buggy and doesn't match the documentation.
-- Some modules (e.g. MobDebug) rely on this bug and attempt to invoke
-- receive()/send() on unconnected sockets.
-- [1]: http://w3.impa.br/~diego/software/luasocket/tcp.html
--
lsocket_tcp_mt.__index.receive = lsocket_tcp_receive;
lsocket_tcp_mt.__index.send = lsocket_tcp_send;

--
-- TCP Constructor and Shortcuts
--

local function lsocket_tcp(self)
    local s = socket_new('AF_INET', 'SOCK_STREAM', 'tcp')
    if not s then
        return nil, socket_error(self)
    end
    return setmetatable(s, lsocket_tcp_mt)
end

local function lsocket_connect(host, port)
    if host == nil or port == nil then
        error("Usage: luasocket.connect(host, port)")
    end
    local s, err = tcp_connect(host, port)
    if not s then
        return nil, err
    end
    setmetatable(s, lsocket_tcp_client_mt)
    return s
end

local function lsocket_bind(host, port, backlog)
    if host == nil or port == nil then
        error("Usage: luasocket.bind(host, port [, backlog])")
    end
    local function prepare(s) return backlog end -- luacheck: no unused args
    local s, err = tcp_server_bind(host, port, prepare)
    if not s then
        return nil, err
    end
    return setmetatable(s, lsocket_tcp_server_mt)
end

--------------------------------------------------------------------------------
-- Module Definition
--------------------------------------------------------------------------------

return setmetatable({
    getaddrinfo = getaddrinfo,
    tcp_connect = tcp_connect,
    tcp_server = tcp_server,
    iowait = internal.iowait,
    internal = internal,
}, {
    __call = function(self, ...) return socket_new(...) end;
    __index = {
        tcp = lsocket_tcp;
        connect = lsocket_connect;
        bind = lsocket_bind;
    }
})
