-- bsdsocket.lua (internal file)

do

local TIMEOUT_INFINITY      = 500 * 365 * 86400

local ffi = require 'ffi'

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
    bsdsocket_local_resolve(const char *host, const char *port,
                            struct sockaddr *addr, socklen_t *socklen);
    int bsdsocket_nonblock(int fd, int mode);

    int setsockopt(int s, int level, int iname, const void *opt, size_t optlen);
    int getsockopt(int s, int level, int iname, void *ptr, size_t *optlen);

    typedef struct { int active; int timeout; } linger_t;

    struct protoent {
        char  *p_name;       /* official protocol name */
        char **p_aliases;    /* alias list */
        int    p_proto;      /* protocol number */
    };
    struct protoent *getprotobyname(const char *name);
]]

ffi.cdef([[
struct sockaddr {
    char _data[256]; /* enough to fit any address */
};
]]);

local function sprintf(fmt, ...)
    return string.format(fmt, ...)
end

local function printf(fmt, ...)
    print(sprintf(fmt, ...))
end

local socket_t = ffi.typeof('struct socket');

local function socket_cdata_gc(socket)
    ffi.C.close(socket.fd)
end

local function check_socket(self)
    local socket = type(self) == 'table' and self.socket
    if not ffi.istype(socket_t, socket) then
        error('Usage: socket:method()');
    end
    return socket.fd
end

local function bless_socket(fd)
    -- Make socket to be non-blocked by default
    if ffi.C.bsdsocket_nonblock(fd, 1) < 0 then
        local errno = box.errno()
        ffi.C.close(fd)
        box.errno(errno)
        return nil
    end

    local socket = ffi.new(socket_t, fd);
    ffi.gc(socket, socket_cdata_gc)
    return setmetatable({ socket = socket }, box.socket.internal.socket_mt)
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
        return box.errno.strerror(self._errno)
    end
end

local addr = ffi.new('struct sockaddr')
local addr_len = ffi.new('socklen_t[1]')
socket_methods.sysconnect = function(self, host, port)
    local fd = check_socket(self)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    addr_len[0] = ffi.sizeof(addr)
    local res = ffi.C.bsdsocket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.connect(fd, addr, addr_len[0]);
        if res == 0 then
            return true
        end
    end
    self._errno = box.errno()
    return false
end

socket_methods.syswrite = function(self, octets)
    local fd = check_socket(self)
    self._errno = nil
    local done = ffi.C.write(fd, octets, string.len(octets))
    if done < 0 then
        self._errno = box.errno()
        return nil
    end
    return tonumber(done)
end

socket_methods.sysread = function(self, len)
    local fd = check_socket(self)
    self._errno = nil
    local buf = ffi.new('char[?]', len)
    local res = ffi.C.read(fd, buf, len)

    if res < 0 then
        self._errno = box.errno()
        return nil
    end

    buf = ffi.string(buf, res)
    return buf
end

socket_methods.nonblock = function(self, nb)
    local fd = check_socket(self)
    self._errno = nil

    local res

    if nb == nil then
        res = ffi.C.bsdsocket_nonblock(fd, 0x80)
    elseif nb then
        res = ffi.C.bsdsocket_nonblock(fd, 1)
    else
        res = ffi.C.bsdsocket_nonblock(fd, 0)
    end

    if res < 0 then
        self._errno = box.errno()
        return nil
    end

    if res == 1 then
        return true
    else
        return false
    end
end

local function wait_safely(self, what, timeout)
    local fd = check_socket(self)
    local f = box.fiber.self()
    local fid = f:id()

    self._errno = nil
    timeout = timeout or TIMEOUT_INFINITY

    if self.waiters == nil then
        self.waiters = {}
    end

    self.waiters[fid] = f
    local wres = box.socket.internal.iowait(fd, what, timeout)
    self.waiters[fid] = nil
    box.fiber.testcancel()

    if wres == 0 then
        self._errno = box.errno.ETIMEDOUT
        return 0
    end
    return wres
end

socket_methods.readable = function(self, timeout)
    return wait_safely(self, 0, timeout) ~= 0
end

socket_methods.wait = function(self, timeout)
    local wres = wait_safely(self, 2, timeout)
    if wres == 0 then
        return nil
    end
    local res = ''
    if bit.band(wres, 1) ~= 0 then
        res = res .. 'R'
    end
    if bit.band(wres, 2) ~= 0 then
        res = res .. 'W'
    end
    return res
end

socket_methods.writable = function(self, timeout)
    return wait_safely(self, 1, timeout) ~= 0
end

socket_methods.listen = function(self, backlog)
    local fd = check_socket(self)
    self._errno = nil
    if backlog == nil then
        backlog = 256
    end
    local res = ffi.C.listen(fd, backlog)
    if res < 0 then
        self._errno = box.errno()
        return false
    end
    return true
end

socket_methods.bind = function(self, host, port)
    local fd = check_socket(self)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    addr_len[0] = ffi.sizeof(addr)
    local res = ffi.C.bsdsocket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.bind(fd, addr, addr_len[0]);
    end
    if res == 0 then
        return true
    end

    self._errno = box.errno()
    return false
end

socket_methods.close = function(self)
    local fd = check_socket(self)
    if self.waiters ~= nil then
        for fid, fiber in pairs(self.waiters) do
            fiber:wakeup()
            self.waiters[fid] = nil
        end
    end

    self._errno = nil
    if ffi.C.close(fd) < 0 then
        self._errno = box.errno()
        return false
    end
    ffi.gc(self.socket, nil)
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
        self._errno = box.errno()
        return false
    end
    return true
end

socket_methods.setsockopt = function(self, level, name, value)
    local fd = check_socket(self)

    local info = get_ivalue(box.socket.internal.SO_OPT, name)

    if info == nil then
        error(sprintf("Unknown socket option name: %s", tostring(name)))
    end

    if not info.rw then
        error(sprintf("Socket option %s is read only", name))
    end

    self._errno = nil

    if type(level) == 'string' then
        if level == 'SOL_SOCKET' then
            level = box.socket.internal.SOL_SOCKET
        else
            local p = ffi.C.getprotobyname(level)
            if p == nil then
                self._errno = box.errno()
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
            self._errno = box.errno()
            return false
        end
        return true
    end

    if info.type == 2 then
        local res = ffi.C.setsockopt(fd,
            level, info.iname, value, ffi.sizeof('size_t'))
        if res < 0 then
            self._errno = box.errno()
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

    local info = get_ivalue(box.socket.internal.SO_OPT, name)

    if info == nil then
        error(sprintf("Unknown socket option name: %s", tostring(name)))
    end

    self._errno = nil

    if type(level) == 'string' then
        if level == 'SOL_SOCKET' then
            level = box.socket.internal.SOL_SOCKET
        else
            local p = ffi.C.getprotobyname(level)
            if p == nil then
                self._errno = box.errno(box.errno.EINVAL)
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
            self._errno = box.errno()
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
            self._errno = box.errno()
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

    local info = box.socket.internal.SO_OPT.SO_LINGER
    self._errno = nil
    if active == nil then
        local value = ffi.new("linger_t[1]")
        local len = ffi.new("size_t[1]", 2 * ffi.sizeof('int'))
        local res = ffi.C.getsockopt(fd,
            box.socket.internal.SOL_SOCKET, info.iname, value, len)
        if res < 0 then
            self._errno = box.errno()
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
        box.socket.internal.SOL_SOCKET, info.iname, value, len)
    if res < 0 then
        self._errno = box.errno()
        return nil
    end

    return active, timeout
end

socket_methods.accept = function(self)
    local fd = check_socket(self)
    self._errno = nil

    local cfd = ffi.C.accept(fd, nil, nil)

    if cfd < 1 then
        self._errno = box.errno()
        return nil
    end
    return bless_socket(cfd)
end

local function readchunk(self, size, timeout)
    if self.rbuf == nil then
        self.rbuf = ''
    end

    if string.len(self.rbuf) >= size then
        self._errno = nil
        local data = string.sub(self.rbuf, 1, size)
        self.rbuf = string.sub(self.rbuf, size + 1)
        return data
    end

    while timeout > 0 do
        local started = box.time()

        if not self:readable(timeout) then
            return nil
        end

        timeout = timeout - ( box.time() - started )

        local data = self:sysread(4096)
        if data ~= nil then
            self.rbuf = self.rbuf .. data
            if string.len(self.rbuf) >= size then
               data = string.sub(self.rbuf, 1, size)
               self.rbuf = string.sub(self.rbuf, size + 1)
               return data
            end

            if string.len(data) == 0 then   -- eof
                data = self.rbuf
                self.rbuf = nil
                return data
            end
        elseif self:errno() ~= box.errno.EAGAIN then
            self._errno = box.errno()
            return nil
        end
    end
    self._errno = box.errno.ETIMEDOUT
    return nil
end

local function readline_check(self, eols, limit)
    local rbuf = self.rbuf
    if rbuf == nil then
        return nil
    end
    if string.len(rbuf) == 0 then
        return nil
    end

    local shortest
    for i, eol in pairs(eols) do
        if string.len(rbuf) >= string.len(eol) then
            local data = string.match(rbuf, "^(.-" .. eol .. ")")
            if data ~= nil then
                if string.len(data) > limit then
                    data = string.sub(data, 1, limit)
                end
                if shortest == nil then
                    shortest = data
                elseif #shortest > #data then
                    shortest = data
                end
            end
        end
    end
    return shortest
end

local function readline(self, limit, eol, timeout)
    if self.rbuf == nil then
        self.rbuf = ''
    end

    self._errno = nil
    local data = readline_check(self, eol, limit)
    if data ~= nil then
        self.rbuf = string.sub(self.rbuf, string.len(data) + 1)
        return data
    end

    local started = box.time()
    while timeout > 0 do
        local started = box.time()

        if not self:readable(timeout) then
            self._errno = box.errno()
            return nil
        end

        timeout = timeout - ( box.time() - started )

        local data = self:sysread(4096)
        if data ~= nil then
            self.rbuf = self.rbuf .. data

            -- eof
            if string.len(data) == 0 then
                data = self.rbuf
                self.rbuf = nil
                return data
            end

            data = readline_check(self, eol, limit)
            if data ~= nil then
                self.rbuf = string.sub(self.rbuf, string.len(data) + 1)
                return data
            end

            -- limit
            if string.len(self.rbuf) >= limit then
               data = string.sub(self.rbuf, 1, limit)
               self.rbuf = string.sub(self.rbuf, limit + 1)
               return data
            end

        elseif self:errno() ~= box.errno.EAGAIN then
            self._errno = box.errno()
            return nil
        end
    end
end

socket_methods.read = function(self, opts, timeout)
    check_socket(self)
    timeout = timeout and tonumber(timeout) or TIMEOUT_INFINITY
    if type(opts) == 'number' then
        return readchunk(self, opts, timeout)
    elseif type(opts) == 'string' then
        return readline(self, 4294967295, { opts }, timeout)
    elseif type(opts) == 'table' then
        local chunk = opts.chunk or opts.size or 4294967295
        local delimiter = opts.delimiter or opts.line
        if delimiter == nil then
            return readchunk(self, chunk, timeout)
        elseif type(delimiter) == 'string' then
            return readline(self, chunk, { delimiter }, timeout)
        elseif type(delimiter) == 'table' then
            return readline(self, chunk, delimiter, timeout)
        end
    end
    error('Usage: s:read(delimiter|chunk|{delimiter = x, chunk = x}, timeout)')
end

socket_methods.write = function(self, octets, timeout)
    check_socket(self)
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local started = box.time()
    while timeout > 0 and self:writable(timeout) do
        timeout = timeout - ( box.time() - started )

        local written = self:syswrite(octets)
        if written == nil then
            if self:errno() ~= box.errno.EAGAIN then
                return false
            end
        end

        if written == string.len(octets) then
            return true
        end
        if written > 0 then
            octets = string.sub(octets, written + 1)
        end
    end
end

socket_methods.send = function(self, octets, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(box.socket.internal.SEND_FLAGS, flags)

    self._errno = nil
    local res = ffi.C.send(fd, octets, string.len(octets), iflags)
    if res == -1 then
        self._errno = box.errno()
        return false
    end
    return true
end

socket_methods.recv = function(self, size, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(box.socket.internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = box.errno.EINVAL
        return nil
    end

    if size == nil then
        size = 512
    end
    self._errno = nil
    local buf = ffi.new("char[?]", size)

    local res = ffi.C.recv(fd, buf, size, iflags)

    if res == -1 then
        self._errno = box.errno()
        return nil
    end
    return ffi.string(buf, res)
end

socket_methods.recvfrom = function(self, size, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(box.socket.internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = box.errno.EINVAL
        return nil
    end
    self._errno = nil
    local res, from = box.socket.internal.recvfrom(fd, size, iflags)
    if res == nil then
        self._errno = box.errno()
        return nil
    end
    return res, from
end

socket_methods.sendto = function(self, host, port, octets, flags)
    local fd = check_socket(self)
    local iflags = get_iflags(box.socket.internal.SEND_FLAGS, flags)

    if iflags == nil then
        self._errno = box.errno.EINVAL
        return nil
    end

    self._errno = nil
    if octets == nil or octets == '' then
        return true
    end

    host = tostring(host)
    port = tostring(port)
    octets = tostring(octets)

    addr_len[0] = ffi.sizeof(addr)
    local res = ffi.C.bsdsocket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.sendto(fd, octets, string.len(octets), iflags,
            addr, addr_len[0])
    end
    if res < 0 then
        self._errno = box.errno()
        return false
    end
    return true
end

local function create_socket(domain, stype, proto)
    local idomain = get_ivalue(box.socket.internal.DOMAIN, domain)
    if idomain == nil then
        box.errno(box.errno.EINVAL)
        return nil
    end

    local itype = get_ivalue(box.socket.internal.SO_TYPE, stype)
    if itype == nil then
        box.errno(box.errno.EINVAL)
        return nil
    end


    local iproto

    if type(proto) == 'number' then
        iproto = proto
    else
        local p = ffi.C.getprotobyname(proto)
        if p == nil then
            box.errno(box.errno.EINVAL)
            return nil
        end
        iproto = p.p_proto
    end

    local fd = ffi.C.socket(idomain, itype, iproto)
    if fd < 1 then
        return nil
    end
    return bless_socket(fd)
end

local function getaddrinfo(host, port, timeout, opts)
    local self = box.socket

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
            local itype = get_ivalue(box.socket.internal.SO_TYPE, opts.type)
            if itype == nil then
                self._errno = box.errno.EINVAL
                return nil
            end
            ga_opts.type = itype
        end

        if opts.family ~= nil then
            local ifamily = get_ivalue(box.socket.internal.DOMAIN, opts.family)
            if ifamily == nil then
                self._errno = box.errno.EINVAL
                return nil
            end
            ga_opts.family = ifamily
        end

        if opts.protocol ~= nil then
            local p = ffi.C.getprotobyname(opts.protocol)
            if p == nil then
                self._errno = box.errno(box.errno.EINVAL)
                return nil
            end
            ga_opts.protocol = p.p_proto
        end

        if opts.flags ~= nil then
            ga_opts.flags =
                get_iflags(box.socket.internal.AI_FLAGS, opts.flags)
            if ga_opts.flags == nil then
                self._errno = box.errno()
                return nil
            end
        end

    end

    local r = box.socket.internal.getaddrinfo(host, port, timeout, ga_opts)
    if r == nil then
        self._errno = box.errno()
    else
        self._errno = nil
    end
    return r
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
    local aka = box.socket.internal.name(fd)
    if aka == nil then
        self._errno = box.errno()
        return nil
    end
    self._errno = nil
    setmetatable(aka, soname_mt)
    return aka
end

socket_methods.peer = function(self)
    local fd = check_socket(self)
    local peer = box.socket.internal.peer(fd)
    if peer == nil then
        self._errno = box.errno()
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
        return s
    end
    local save_errno = s:errno()
    if save_errno ~= box.errno.EINPROGRESS then
        s:close()
        box.errno(save_errno)
        return nil
    end
    res = s:wait(timeout)
    save_errno = s:errno()
    -- When the connection completes succesfully, the descriptor becomes
    -- writable. When the connection establishment encounters an error,
    -- the descriptor becomes both readable and writable (Stevens UNP).
    -- However, in real life if server sends some data immediately on connect,
    -- descriptor can also become RW. For this case we also check SO_ERROR.
    if res ~= nil and res ~= 'W' then
        save_errno = s:getsockopt('SOL_SOCKET', 'SO_ERROR')
    end
    if save_errno ~= 0 then
        s:close()
        box.errno(save_errno)
        return nil
    end
    -- Connected
    box.errno(0)
    return s
end

local function tcp_connect(host, port, timeout)
    if host == 'unix/' then
        return tcp_connect_remote({ host = host, port = port, protocol = 'ip',
            family = 'PF_UNIX', type = 'SOCK_STREAM' }, timeout)
    end
    local timeout = timeout or TIMEOUT_INFINITY
    local stop = box.time() + timeout
    local dns = getaddrinfo(host, port, timeout, { type = 'SOCK_STREAM',
        protocol = 'tcp' })
    if dns == nil then
        return nil
    end
    for i, remote in pairs(dns) do
        timeout = stop - box.time()
        if timeout <= 0 then
            box.errno(box.errno.ETIMEDOUT)
            return nil
        end
        local s = tcp_connect_remote(remote, timeout)
        if s then
            return s
        end
    end
    return nil
end

local function tcp_server_remote(list, prepare, handler)
    local slist = {}

    -- bind/create sockets
    for _, addr in pairs(list) do
        local s = create_socket(addr.family, addr.type, addr.protocol)

        local ok = false
        if s ~= nil then
            local backlog = prepare(s)
            if s:bind(addr.host, addr.port) then
                if s:listen(backlog) then
                    ok = true
                end
            end
        end

        -- errors
        if not ok then
            if s ~= nil then
                s:close()
            end
            local save_errno = box.errno()
            for _, s in pairs(slist) do
                s:close()
            end
            box.errno(save_errno)
            return nil
        end

        table.insert(slist, s)
    end
    
    local server = { s = slist }

    server.stop = function()
        if #server.s == 0 then
            return false
        end
        for _, s in pairs(server.s) do
            s:close()
        end
        server.s = {}
        return true
    end

    for _, s in pairs(server.s) do
        box.fiber.wrap(function(s)
            box.fiber.name(sprintf("listen_fd=%d", s:fd()))

            while s:readable() do
                
                local sc = s:accept()

                if sc == nil then
                    break
                end

                box.fiber.wrap(function(sc)
                    pcall(handler, sc)
                    sc:close()
                end, sc)
            end
        end, s)
    end

    return server

end

local function tcp_server(host, port, prepare, handler, timeout)
    if handler == nil then
        handler = prepare
        prepare = function() end
    end

    if type(prepare) ~= 'function' or type(handler) ~= 'function' then
        error("Usage: socket.tcp_server(host, port[, prepare], handler)")
    end

    if host == 'unix/' then
        return tcp_server_remote({{host = host, port = port, protocol = 0,
            family = 'PF_UNIX', type = 'SOCK_STREAM' }}, prepare, handler)
    end

    local dns = getaddrinfo(host, port, timeout, { type = 'SOCK_STREAM',
        protocol = 'tcp' })
    if dns == nil then
        return nil
    end
    return tcp_server_remote(dns, prepare, handler)
end

box.socket.internal = {
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
        end
    },
}

setmetatable(box.socket, {
    __call = function(self, ...) return create_socket(...) end,
    __index = {
        getaddrinfo = getaddrinfo,
        tcp_connect = tcp_connect,
        tcp_server  = tcp_server
    }
})

end
