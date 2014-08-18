-- bsdsocket.lua (internal file)

do

local TIMEOUT_INFINITY      = 500 * 365 * 86400

local ffi = require('ffi')
local boxerrno = require('errno')
local internal = require('socket.internal')
local fiber = require('fiber')

ffi.cdef[[
    typedef uint32_t socklen_t;
    typedef ptrdiff_t ssize_t;

    int connect(int sockfd, const struct sockaddr *addr,
                socklen_t addrlen);
    int bind(int sockfd, const struct sockaddr *addr,
                socklen_t addrlen);

    ssize_t write(int fh, const char *octets, size_t len);
    ssize_t read(int fd, void *buf, size_t count);
    int listen(int fh, int backlog);
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
    int bsdsocket_nonblock(int fh, int mode);

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

local socket_mt
local socket_methods  = {}
socket_methods.errno = function(self)
    if self['_errno'] == nil then
        return 0
    else
        return self['_errno']
    end
end

socket_methods.error = function(self)
    if self['_errno'] == nil then
        return nil
    else
        return boxerrno.strerror(self._errno)
    end
end

local addr = ffi.new('struct sockaddr')
local addr_len = ffi.new('socklen_t[1]')
socket_methods.sysconnect = function(self, host, port)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    addr_len[0] = ffi.sizeof(addr)
    local res = ffi.C.bsdsocket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.connect(self.fh, addr, addr_len[0]);
        if res == 0 then
            return true
        end
    end
    self._errno = boxerrno()
    return false
end

socket_methods.syswrite = function(self, octets)
    self._errno = nil
    local done = ffi.C.write(self.fh, octets, string.len(octets))
    if done < 0 then
        self._errno = boxerrno()
        return nil
    end
    return tonumber(done)
end

socket_methods.sysread = function(self, len)
    self._errno = nil
    local buf = ffi.new('char[?]', len)
    local res = ffi.C.read(self.fh, buf, len)

    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    buf = ffi.string(buf, res)
    return buf
end

socket_methods.nonblock = function(self, nb)
    self._errno = nil

    local res

    if nb == nil then
        res = ffi.C.bsdsocket_nonblock(self.fh, 0x80)
    elseif nb then
        res = ffi.C.bsdsocket_nonblock(self.fh, 1)
    else
        res = ffi.C.bsdsocket_nonblock(self.fh, 0)
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

local function wait_safely(self, what, timeout)
    local f = fiber.self()
    local fid = f:id()

    if self.waiters == nil then
        self.waiters = {}
    end

    self.waiters[fid] = true
    local res = internal.iowait(self.fh, what, timeout)
    self.waiters[fid] = nil
    fiber.testcancel()
    return res
end

socket_methods.readable = function(self, timeout)
    self._errno = nil
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local wres = wait_safely(self, 0, timeout)

    if wres == 0 then
        self._errno = boxerrno.ETIMEDOUT
        return false
    end
    return true
end

socket_methods.wait = function(self, timeout)
    self._errno = nil
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local wres = wait_safely(self, 2, timeout)

    if wres == 0 then
        self._errno = boxerrno.ETIMEDOUT
        return
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
    self._errno = nil
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local wres = wait_safely(self, 1, timeout)

    if wres == 0 then
        self._errno = boxerrno.ETIMEDOUT
        return false
    end
    return true
end

socket_methods.listen = function(self, backlog)
    self._errno = nil
    if backlog == nil then
        backlog = 256
    end
    local res = ffi.C.listen(self.fh, backlog)
    if res < 0 then
        self._errno = boxerrno()
        return false
    end
    return true
end

socket_methods.bind = function(self, host, port)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    addr_len[0] = ffi.sizeof(addr)
    local res = ffi.C.bsdsocket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.bind(self.fh, addr, addr_len[0]);
    end
    if res == 0 then
        return true
    end

    self._errno = boxerrno()
    return false
end

socket_methods.close = function(self)
    if self.waiters ~= nil then
        for fid in pairs(self.waiters) do
            internal.abort(fid)
            self.waiters[fid] = nil
        end
    end

    self._errno = nil
    if ffi.C.close(self.fh) < 0 then
        self._errno = boxerrno()
        return false
    end
    return true
end

socket_methods.shutdown = function(self, how)
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
    if ffi.C.shutdown(self.fh, ihow) < 0 then
        self._errno = boxerrno()
        return false
    end
    return true
end

socket_methods.setsockopt = function(self, level, name, value)
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
        local res = ffi.C.setsockopt(self.fh,
            level, info.iname, value, ffi.sizeof('int'))

        if res < 0 then
            self._errno = boxerrno()
            return false
        end
        return true
    end

    if info.type == 2 then
        local res = ffi.C.setsockopt(self.fh,
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
        local res = ffi.C.getsockopt(self.fh, level, info.iname, value, len)

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
        local res = ffi.C.getsockopt(self.fh, level, info.iname, value, len)
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

    local info = internal.SO_OPT.SO_LINGER
    self._errno = nil
    if active == nil then
        local value = ffi.new("linger_t[1]")
        local len = ffi.new("size_t[1]", 2 * ffi.sizeof('int'))
        local res = ffi.C.getsockopt(self.fh,
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
    local res = ffi.C.setsockopt(self.fh,
        internal.SOL_SOCKET, info.iname, value, len)
    if res < 0 then
        self._errno = boxerrno()
        return nil
    end

    return active, timeout
end

socket_methods.accept = function(self)

    self._errno = nil

    local fh = ffi.C.accept(self.fh, nil, nil)

    if fh < 1 then
        self._errno = boxerrno()
        return nil
    end

    fh = tonumber(fh)

    -- Make socket to be non-blocked by default
    -- ignore result
    ffi.C.bsdsocket_nonblock(fh, 1)

    local socket = { fh = fh }
    setmetatable(socket, socket_mt)
    return socket
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
        local started = fiber.time()

        if not self:readable(timeout) then
            return nil
        end

        timeout = timeout - ( fiber.time() - started )

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
        elseif self:errno() ~= boxerrno.EAGAIN then
            self._errno = boxerrno()
            return nil
        end
    end
    self._errno = boxerrno.ETIMEDOUT
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
    for i, eol in pairs(eols) do
        if string.len(rbuf) >= string.len(eol) then
            local data = string.match(rbuf, "^(.-" .. eol .. ")")
            if data ~= nil then
                if string.len(data) > limit then
                    return string.sub(data, 1, limit)
                end
                return data
            end
        end
    end
    return nil
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

    local started = fiber.time()
    while timeout > 0 do
        local started = fiber.time()
        
        if not self:readable(timeout) then
            self._errno = boxerrno()
            return nil
        end
        
        timeout = timeout - ( fiber.time() - started )

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

        elseif self:errno() ~= boxerrno.EAGAIN then
            self._errno = boxerrno()
            return nil
        end
    end
end

socket_methods.read = function(self, opts, timeout)
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
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local started = fiber.time()
    while timeout > 0 and self:writable(timeout) do
        timeout = timeout - ( fiber.time() - started )

        local written = self:syswrite(octets)
        if written == nil then
            if self:errno() ~= boxerrno.EAGAIN then
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
    local iflags = get_iflags(internal.SEND_FLAGS, flags)

    self._errno = nil
    local res = ffi.C.send(self.fh, octets, string.len(octets), iflags)
    if res == -1 then
        self._errno = boxerrno()
        return false
    end
    return true
end

socket_methods.recv = function(self, size, flags)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end

    if size == nil then
        size = 512
    end
    self._errno = nil
    local buf = ffi.new("char[?]", size)

    local res = ffi.C.recv(self.fh, buf, size, iflags)

    if res == -1 then
        self._errno = boxerrno()
        return nil
    end
    return ffi.string(buf, res)
end

socket_methods.recvfrom = function(self, size, flags)
    local iflags = get_iflags(internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = boxerrno.EINVAL
        return nil
    end
    self._errno = nil
    local res, from = internal.recvfrom(self.fh, size, iflags)
    if res == nil then
        self._errno = boxerrno()
        return nil
    end
    return res, from
end

socket_methods.sendto = function(self, host, port, octets, flags)
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

    addr_len[0] = ffi.sizeof(addr)
    local res = ffi.C.bsdsocket_local_resolve(host, port, addr, addr_len)
    if res == 0 then
        res = ffi.C.sendto(self.fh, octets, string.len(octets), iflags,
            addr, addr_len[0])
    end
    if res < 0 then
        self._errno = boxerrno()
        return false
    end
    return true
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

    local fh = ffi.C.socket(idomain, itype, iproto)
    if fh < 0 then
        return nil
    end

    fh = tonumber(fh)

    -- Make socket to be non-blocked by default
    if ffi.C.bsdsocket_nonblock(fh, 1) < 0 then
        local errno = boxerrno()
        ffi.C.close(fh)
        boxerrno(errno)
        return nil
    end

    local socket = { fh = fh }
    setmetatable(socket, socket_mt)
    return socket
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
                self._errno = boxerrno.EINVAL
                return nil
            end
            ga_opts.type = itype
        end

        if opts.family ~= nil then
            local ifamily = get_ivalue(internal.DOMAIN, opts.family)
            if ifamily == nil then
                self._errno = boxerrno.EINVAL
                return nil
            end
            ga_opts.family = ifamily
        end

        if opts.protocol ~= nil then
            local p = ffi.C.getprotobyname(opts.protocol)
            if p == nil then
                self._errno = boxerrno(boxerrno.EINVAL)
                return nil
            end
            ga_opts.protocol = p.p_proto
        end

        if opts.flags ~= nil then
            ga_opts.flags =
                get_iflags(internal.AI_FLAGS, opts.flags)
            if ga_opts.flags == nil then
                self._errno = boxerrno()
                return nil
            end
        end

    end
    
    return internal.getaddrinfo(host, port, timeout, ga_opts)
end

socket_methods.name = function(self)
    local aka = internal.name(self.fh)
    if aka == nil then
        self._errno = boxerrno()
        return nil
    end
    self._errno = nil
    return aka
end

socket_methods.peer = function(self)
    local peer = internal.peer(self.fh)
    if peer == nil then
        self._errno = boxerrno()
        return nil
    end
    self._errno = nil
    return peer
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
    if dns == nil then
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
    return nil
end

socket_mt = {
        __index     = socket_methods,
        __tostring  = function(self)
            local save_errno = self._errno
            local name = sprintf("fd %d", self.fh)
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
}

if package.loaded.socket == nil then
    package.loaded.socket = {}
end

package.loaded.socket.tcp_connect = tcp_connect
-- package.loaded.socket.tcp_server = tcp_server

setmetatable(package.loaded.socket, {
    __call = function(self, ...) return create_socket(...) end,
    __index = {
        getaddrinfo = getaddrinfo,
    }
})

end
