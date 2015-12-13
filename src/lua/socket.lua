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

ffi.cdef[[
    struct socket {
        int fd;
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
    int close(int s);
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

local function sprintf(fmt, ...)
    return string.format(fmt, ...)
end

local function printf(fmt, ...)
    print(sprintf(fmt, ...))
end

local socket_t = ffi.typeof('struct socket');

local function socket_cdata_gc(socket)
    if socket.fd < 0 then
        log.error("socket: attempt to double close on gc")
        return
    end
    if ffi.C.close(socket.fd) ~= 0 then
        log.error("socket: failed to close fd=%d on gc: %s", socket.fd,
            boxerrno.strerror())
    end
end

local function check_socket(self)
    local socket = type(self) == 'table' and self.socket
    if not ffi.istype(socket_t, socket) then
        error('Usage: socket:method()');
    end
    if socket.fd < 0 then
        error("attempt to use closed socket")
    end
    return socket.fd
end

local socket_mt
local function bless_socket(fd)
    -- Make socket to be non-blocked by default
    if ffi.C.lbox_socket_nonblock(fd, 1) < 0 then
        local errno = boxerrno()
        ffi.C.close(fd)
        boxerrno(errno)
        return nil
    end

    local socket = ffi.new(socket_t, { fd = fd });
    ffi.gc(socket, socket_cdata_gc)
    return setmetatable({ socket = socket }, socket_mt)
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
    for i, f in pairs(flags) do
        if table[f] == nil then
            return nil
        end
        res = bit.bor(res, table[f])
    end
    return res
end

local socket_methods  = {}
socket_methods.errno = function(self)
    check_socket(self)
    if self['_errno'] == nil then
        return 0
    else
        return self['_errno']
    end
end

socket_methods.error = function(self)
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
socket_methods.sysconnect = function(self, host, port)
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

socket_methods.syswrite = function(self, arg1, arg2)
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

socket_methods.sysread = function(self, arg1, arg2)
    -- TODO: ffi.istype('char *', arg1) doesn't work for ffi.new('char[256]')
    if type(arg1) == 'cdata' and arg2 ~= nil then
        return sysread(self, arg1, arg2)
    end

    local size = arg1 or buffer.READAHEAD

    local buf = buffer.IBUF_SHARED
    buf:reset()
    local p = buf:alloc(size)

    local res = sysread(self, p, size)
    if res then
        local str = ffi.string(p, res)
        buf:recycle()
        return str
    else
        buf:recycle()
        return res
    end
end

socket_methods.nonblock = function(self, nb)
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

local waiters_mt = { __serialize = 'mapping' }
local function wait_safely(self, what, timeout)
    local fd = check_socket(self)
    local f = fiber.self()
    local fid = f:id()

    self._errno = nil
    timeout = timeout or TIMEOUT_INFINITY

    if self.waiters == nil then
        self.waiters = setmetatable({}, waiters_mt)
    end

    self.waiters[fid] = true
    local res = internal.iowait(fd, what, timeout)
    self.waiters[fid] = nil
    if res == 0 then
        self._errno = boxerrno.ETIMEDOUT
        return 0
    end
    fiber.testcancel()
    return res
end

socket_methods.readable = function(self, timeout)
    return wait_safely(self, 1, timeout) ~= 0
end

socket_methods.wait = function(self, timeout)
    return wait_safely(self, 'RW', timeout)
end

socket_methods.writable = function(self, timeout)
    return wait_safely(self, 2, timeout) ~= 0
end

socket_methods.listen = function(self, backlog)
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

socket_methods.bind = function(self, host, port)
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

socket_methods.close = function(self)
    local fd = check_socket(self)
    if self.waiters ~= nil then
        for fid in pairs(self.waiters) do
            internal.abort(fid)
            self.waiters[fid] = nil
        end
    end

    self._errno = nil
    local r = ffi.C.close(fd)
    self.socket.fd = -1
    ffi.gc(self.socket, nil)
    if r ~= 0 then
        self._errno = boxerrno()
        return false
    end
    return true
end

socket_methods.shutdown = function(self, how)
    local fd = check_socket(self)
    local hvariants = {
        ['R']           = 0,
        ['READ']        = 0,
        ['W']           = 1,
        ['WRITE']       = 1,
        ['RW']          = 2,
        ['READ_WRITE']  = 2,

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

socket_methods.setsockopt = function(self, level, name, value)
    local fd = check_socket(self)

    local info = get_ivalue(internal.SO_OPT, name)

    if info == nil then
        error(sprintf("Unknown socket option name: %s", tostring(name)))
    end

    if not info.rw then
        error(sprintf("Socket option %s is read only", name))
    end

    self._errno = nil

    if type(level) == 'string' then
        if level == 'SOL_SOCKET' then
            level = internal.SOL_SOCKET
        else
            local p = ffi.C.getprotobyname(level)
            if p == nil then
                self._errno = boxerrno()
                return false
            end
            level = p.p_proto
        end
    else
        level = tonumber(level)
    end

    if type(value) == 'boolean' then
        if value then
            value = 1
        else
            value = 0
        end
    end

    if info.type == 1 then
        local value = ffi.new("int[1]", value)
        local res = ffi.C.setsockopt(fd,
            level, info.iname, value, ffi.sizeof('int'))

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
    error(sprintf("Unsupported socket option: %s", name))
end

socket_methods.getsockopt = function(self, level, name)
    local fd = check_socket(self)

    local info = get_ivalue(internal.SO_OPT, name)

    if info == nil then
        error(sprintf("Unknown socket option name: %s", tostring(name)))
    end

    self._errno = nil

    if type(level) == 'string' then
        if level == 'SOL_SOCKET' then
            level = internal.SOL_SOCKET
        else
            local p = ffi.C.getprotobyname(level)
            if p == nil then
                self._errno = boxerrno(boxerrno.EINVAL)
                return nil
            end
            level = p.p_proto
        end
    else
        level = tonumber(level)
    end


    if info.type == 1 then
        local value = ffi.new("int[1]", 0)
        local len = ffi.new("size_t[1]", ffi.sizeof('int'))
        local res = ffi.C.getsockopt(fd, level, info.iname, value, len)

        if res < 0 then
            self._errno = boxerrno()
            return nil
        end

        if len[0] ~= 4 then
            error(sprintf("Internal error: unexpected optlen: %d", len[0]))
        end
        return tonumber(value[0])
    end

    if info.type == 2 then
        local value = ffi.new("char[256]", { 0 })
        local len = ffi.new("size_t[1]", 256)
        local res = ffi.C.getsockopt(fd, level, info.iname, value, len)
        if res < 0 then
            self._errno = boxerrno()
            return nil
        end
        return ffi.string(value, tonumber(len[0]))
    end

    if name == 'SO_LINGER' then
        error("Use s:linger()")
    end
    error(sprintf("Unsupported socket option: %s", name))
end

socket_methods.linger = function(self, active, timeout)
    local fd = check_socket(self)

    local info = internal.SO_OPT.SO_LINGER
    self._errno = nil
    if active == nil then
        local value = ffi.new("linger_t[1]")
        local len = ffi.new("size_t[1]", 2 * ffi.sizeof('int'))
        local res = ffi.C.getsockopt(fd,
            internal.SOL_SOCKET, info.iname, value, len)
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
    local res = ffi.C.setsockopt(fd,
        internal.SOL_SOCKET, info.iname, value, len)
    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    return active, timeout
end

socket_methods.accept = function(self)
    local fd = check_socket(self)
    self._errno = nil

    local cfd, from = internal.accept(fd)
    if cfd == nil then
        self._errno = boxerrno()
        return nil
    end
    local c = bless_socket(cfd)
    if c == nil then
        self._errno = boxerrno()
        return nil
    end
    return c, from
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

local function check_delimiter(self, limit, eols)
    if limit == 0 then
        return 0
    end
    local rbuf = self.rbuf
    if rbuf:size() == 0 then
        return nil
    end

    local shortest
    for i, eol in ipairs(eols) do
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
    assert(limit >= 0)
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

    local started = fiber.time()
    while timeout > 0 do
        local started = fiber.time()

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
        elseif not errno_is_transient[self:errno()] then
            self._errno = boxerrno()
            return nil
        end

        if not self:readable(timeout) then
            return nil
        end
        if timeout <= 0 then
            break
        end
        timeout = timeout - ( fiber.time() - started )
    end
    self._errno = boxerrno.ETIMEDOUT
    return nil
end

socket_methods.read = function(self, opts, timeout)
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

socket_methods.write = function(self, octets, timeout)
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

    local started = fiber.time()
    while true do
        local written = syswrite(self, p, e - p)
        if written == 0 then
            return p - s -- eof
        elseif written ~= nil then
            p = p + written
            assert(p <= e)
            if p == e then
                return e - s
            end
        elseif not errno_is_transient[self:errno()] then
            return nil
        end

        timeout = timeout - (fiber.time() - started)
        if timeout <= 0 or not self:writable(timeout) then
            break
        end
    end
end

socket_methods.send = function(self, octets, flags)
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

socket_methods.recv = function(self, size, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end

    size = size or 512
    self._errno = nil
    local buf = ffi.new("char[?]", size)

    local res = ffi.C.recv(fd, buf, size, iflags)

    if res == -1 then
        self._errno = boxerrno()
        return nil
    end
    return ffi.string(buf, res)
end

socket_methods.recvfrom = function(self, size, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end

    size = size or 512
    self._errno = nil
    local res, from = internal.recvfrom(fd, size, iflags)
    if res == nil then
        self._errno = boxerrno()
        return nil
    end
    return res, from
end

socket_methods.sendto = function(self, host, port, octets, flags)
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

local function create_socket(domain, stype, proto)
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

    local iproto
    if type(proto) == 'string' then
        local p = ffi.C.getprotobyname(proto)
        if p == nil then
            boxerrno(boxerrno.EINVAL)
            return nil
        end
        iproto = p.p_proto
    else
        iproto = tonumber(proto)
    end

    local fd = ffi.C.socket(idomain, itype, iproto)
    if fd < 1 then
        return nil
    end
    return bless_socket(fd)
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
                return nil
            end
            ga_opts.type = itype
        end

        if opts.family ~= nil then
            local ifamily = get_ivalue(internal.DOMAIN, opts.family)
            if ifamily == nil then
                boxerrno(boxerrno.EINVAL)
                return nil
            end
            ga_opts.family = ifamily
        end

        if opts.protocol ~= nil then
            local p = ffi.C.getprotobyname(opts.protocol)
            if p == nil then
                boxerrno(boxerrno.EINVAL)
                return nil
            end
            ga_opts.protocol = p.p_proto
        end

        if opts.flags ~= nil then
            ga_opts.flags =
                get_iflags(internal.AI_FLAGS, opts.flags)
            if ga_opts.flags == nil then
                boxerrno(boxerrno.EINVAL)
                return nil
            end
        end

    end
    return internal.getaddrinfo(host, port, timeout, ga_opts)
end

local soname_mt = {
    __tostring = function(si)
        if si.host == nil and si.port == nil then
            return ''
        end
        if si.host == nil then
            return sprintf('%s:%s', '0', tostring(si.port))
        end

        if si.port == nil then
            return sprintf('%s:%', tostring(si.host), 0)
        end
        return sprintf('%s:%s', tostring(si.host), tostring(si.port))
    end
}

socket_methods.name = function(self)
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

socket_methods.peer = function(self)
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

socket_methods.fd = function(self)
    return check_socket(self)
end

-- tcp connector
local function tcp_connect_remote(remote, timeout)
    local s = create_socket(remote.family, remote.type, remote.protocol)
    if not s then
        -- Address family is not supported by the host
        return nil
    end
    local res = s:sysconnect(remote.host, remote.port)
    if res then
        -- Even through the socket is nonblocking, if the server to which we
        -- are connecting is on the same host, the connect is normally
        -- established immediately when we call connect (Stevens UNP).
        boxerrno(0)
        return s
    end
    local save_errno = s:errno()
    if save_errno ~= boxerrno.EINPROGRESS then
        s:close()
        boxerrno(save_errno)
        return nil
    end
    -- Wait until the connection is established or ultimately fails.
    -- In either condition the socket becomes writable. To tell these
    -- conditions appart SO_ERROR must be consulted (man connect).
    if s:writable(timeout) then
        save_errno = s:getsockopt('SOL_SOCKET', 'SO_ERROR')
    else
        save_errno = boxerrno.ETIMEDOUT
    end
    if save_errno ~= 0 then
        s:close()
        boxerrno(save_errno)
        return nil
    end
    -- Connected
    boxerrno(0)
    return s
end

local function tcp_connect(host, port, timeout)
    if host == 'unix/' then
        return tcp_connect_remote({ host = host, port = port, protocol = 0,
            family = 'PF_UNIX', type = 'SOCK_STREAM' }, timeout)
    end
    local timeout = timeout or TIMEOUT_INFINITY
    local stop = fiber.time() + timeout
    local dns = getaddrinfo(host, port, timeout, { type = 'SOCK_STREAM',
        protocol = 'tcp' })
    if dns == nil or #dns == 0 then
        boxerrno(boxerrno.EINVAL)
        return nil
    end
    for i, remote in pairs(dns) do
        timeout = stop - fiber.time()
        if timeout <= 0 then
            boxerrno(boxerrno.ETIMEDOUT)
            return nil
        end
        local s = tcp_connect_remote(remote, timeout)
        if s then
            return s
        end
    end
    -- errno is set by tcp_connect_remote()
    return nil
end

local function tcp_server_handler(server, sc, from)
    fiber.name(sprintf("%s/%s:%s", server.name, from.host, from.port))
    server.handler(sc, from)
    sc:shutdown()
    sc:close()
end

local function tcp_server_loop(server, s, addr)
    fiber.name(sprintf("%s/%s:%s", server.name, addr.host, addr.port))
    log.info("started")
    while s:readable() do
        local sc, from = s:accept()
        if sc == nil then
            local errno = s:errno()
            if not errno_is_transient[errno] then
                log.error('accept(%s) failed: %s', tostring(s), s:error())
            end
            if  errno_is_fatal[errno] then
                break
            end
        else
            fiber.create(tcp_server_handler, server, sc, from)
        end
    end
    -- Socket was closed
    if addr.family == 'AF_UNIX' and addr.port then
        fio.unlink(addr.port) -- remove unix socket
    end
    log.info("stopped")
end

local function tcp_server_usage()
    error('Usage: socket.tcp_server(host, port, handler | opts)')
end

local function tcp_server_bind(s, addr)
    if s:bind(addr.host, addr.port) then
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
    return s:bind(addr.host, addr.port)
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
    timeout = timeout and tonumber(timeout) or TIMEOUT_INFINITY
    local dns
    if host == 'unix/' then
        dns = {{host = host, port = port, family = 'AF_UNIX', protocol = 0,
            type = 'SOCK_STREAM' }}
    else
        dns = getaddrinfo(host, port, timeout, { type = 'SOCK_STREAM',
            flags = 'AI_PASSIVE'})
        if dns == nil then
            return nil
        end
    end

    for _, addr in ipairs(dns) do
        local s = create_socket(addr.family, addr.type, addr.protocol)
        if s ~= nil then
            local backlog
            if server.prepare then
                backlog = server.prepare(s)
            else
                s:setsockopt('SOL_SOCKET', 'SO_REUSEADDR', 1) -- ignore error
            end
            if not tcp_server_bind(s, addr) or not s:listen(backlog) then
                local save_errno = boxerrno()
                s:close()
                boxerrno(save_errno)
                return nil
            end
            fiber.create(tcp_server_loop, server, s, addr)
            return s, addr
       end
    end
    -- DNS resolved successfully, but addresss family is not supported
    boxerrno(boxerrno.EAFNOSUPPORT)
    return nil
end

socket_mt   = {
    __index     = socket_methods,
    __tostring  = function(self)
        local fd = check_socket(self)

        local save_errno = self._errno
        local name = sprintf("fd %d", fd)
        local aka = self:name()
        if aka ~= nil then
            name = sprintf("%s, aka %s:%s", name, aka.host, aka.port)
        end
        local peer = self:peer(self)
        if peer ~= nil then
            name = sprintf("%s, peer %s:%s", name, peer.host, peer.port)
        end
        self._errno = save_errno
        return name
    end,
    __serialize = function(self)
        -- Allow YAML, MsgPack and JSON to dump objects with sockets
        local fd = check_socket(self)
        return { fd = fd, peer = self:peer(), name = self:name() }
    end
}

return setmetatable({
    getaddrinfo = getaddrinfo,
    tcp_connect = tcp_connect,
    tcp_server = tcp_server,
    iowait = internal.iowait
}, {
    __call = function(self, ...) return create_socket(...) end;
})
