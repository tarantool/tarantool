-- bsdsocket.lua (internal file)

do

local TIMEOUT_INFINITY      = 500 * 365 * 86400

local ffi = require 'ffi'

ffi.cdef[[
    int write(int fh, const char *octets, size_t len);
    int read(int fh, char *buf, size_t len);
    int listen(int fh, int backlog);
    int socket(int domain, int type, int protocol);
    int close(int s);
    int shutdown(int s, int how);
    int send(int s, const void *msg, size_t len, int flags);
    int recv(int s, void *buf, size_t len, int flags);
    int accept(int s, void *addr, void *addrlen);

    int bsdsocket_protocol(const char *proto);
    int bsdsocket_sysconnect(int fh, const char *host, const char *port);
    int bsdsocket_bind(int fh, const char *host, const char *port);
    int bsdsocket_nonblock(int fh, int mode);
    int bsdsocket_sendto(int fh, const char *host, const char *port,
	const void *octets, size_t len, int flags);


    int setsockopt(int s, int level, int iname, const void *opt, size_t optlen);
    int getsockopt(int s, int level, int iname, void *ptr, size_t *optlen);


    typedef struct { int active; int timeout; } linger_t;
]]

local function sprintf(fmt, ...)
    return string.format(fmt, ...)
end

local function printf(fmt, ...)
    print(sprintf(fmt, ...))
end


local function errno(self)
    if self['_errno'] == nil then
        return 0
    else
        return self['_errno']
    end
end

local function errstr(self)
    if self['_errno'] == nil then
        return nil
    else
        return box.errno.strerror(self._errno)
    end
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
socket_methods.errno = errno
socket_methods.error = errstr
    
socket_methods.sysconnect = function(self, host, port)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    local res = ffi.C.bsdsocket_sysconnect(self.fh, host, port)
    if res == 0 then
        return true
    end

    local errno = box.errno()
    if errno == box.errno.EINPROGRESS then
        return true
    end

    self._errno = errno
    return false
end

socket_methods.syswrite = function(self, octets)
    self._errno = nil
    local done = ffi.C.write(self.fh, octets, string.len(octets))
    if done < 0 then
        self._errno = box.errno()
        return nil
    end
    return done
end

socket_methods.sysread = function(self, len)
    self._errno = nil
    local buf = ffi.new('char[?]', len)
    local res = ffi.C.read(self.fh, buf, len)
    
    if res < 0 then
        self._errno = box.errno()
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
        self._errno = box.errno()
        return nil
    end

    if res == 1 then
        return true
    else
        return false
    end
end

socket_methods.readable = function(self, timeout)
    self._errno = nil
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local wres = box.socket.internal.iowait(self.fh, 0, timeout)

    if wres == 0 then
        self._errno = box.errno.ETIMEDOUT
        return false
    end
    return true
end

socket_methods.wait = function(self, timeout)
    self._errno = nil
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    local wres = box.socket.internal.iowait(self.fh, 2, timeout)

    if wres == 0 then
        self._errno = box.errno.ETIMEDOUT
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

    local wres = box.socket.internal.iowait(self.fh, 1, timeout)

    if wres == 0 then
        self._errno = box.errno.ETIMEDOUT
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
        self._errno = box.errno()
        return false
    end
    return true
end

socket_methods.bind = function(self, host, port)
    self._errno = nil

    host = tostring(host)
    port = tostring(port)

    local res = ffi.C.bsdsocket_bind(self.fh, host, port)
    if res == 0 then
        return true
    end
    self._errno = box.errno()
    return false
end

socket_methods.close = function(self)
    self._errno = nil
    if ffi.C.close(self.fh) < 0 then
        self._errno = box.errno()
        return false
    else
        return true
    end
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
        ihow = 3
    end
    self._errno = nil
    if ffi.C.shutdown(self.fh, ihow) < 0 then
        self._errno = box.errno()
        return false
    end
    return true
end

socket_methods.setsockopt = function(self, level, name, value)
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
            level = ffi.C.bsdsocket_protocol(level)
            if level == -1 then
                self._errno = box.errno()
                return false
            end
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
            level, info.iname, value, box.socket.internal.INT_SIZE)

        if res < 0 then
            self._errno = box.errno()
            return false
        end
        return true
    end

    if info.type == 2 then
        local res = ffi.C.setsockopt(self.fh,
            level, info.iname, value, box.socket.internal.SIZE_T_SIZE)
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
    local info = get_ivalue(box.socket.internal.SO_OPT, name)

    if info == nil then
        error(sprintf("Unknown socket option name: %s", tostring(name)))
    end

    self._errno = nil

    if type(level) == 'string' then
        if level == 'SOL_SOCKET' then
            level = box.socket.internal.SOL_SOCKET
        else
            level = ffi.C.bsdsocket_protocol(level)
            if level == -1 then
                self._errno = box.errno()
                return nil
            end
        end
    else
        level = tonumber(level)
    end


    if info.type == 1 then
        local value = ffi.new("int[1]", 0)
        local len = ffi.new("size_t[1]", box.socket.internal.INT_SIZE)
        local res = ffi.C.getsockopt(self.fh, level, info.iname, value, len)

        if res < 0 then
            self._errno = box.errno()
            return nil
        end

        if len[0] ~= 4 then
            error(sprintf("Internal error: unexpected optlen: %d", len[0]))
        end
        return value[0]
    end

    if info.type == 2 then
        local value = ffi.new("char[256]", { 0 })
        local len = ffi.new("size_t[1]", 256)
        local res = ffi.C.getsockopt(self.fh, level, info.iname, value, len)
        if res < 0 then
            self._errno = box.errno()
            return nil
        end
        return ffi.string(value, len[0])
    end

    if name == 'SO_LINGER' then
        error("Use s:linger()")
    end
    error(sprintf("Unsupported socket option: %s", name))
end

socket_methods.linger = function(self, active, timeout)

    local info = box.socket.internal.SO_OPT.SO_LINGER
    self._errno = nil
    if active == nil then
        local value = ffi.new("linger_t[1]")
        local len = ffi.new("size_t[1]", 2 * box.socket.internal.INT_SIZE)
        local res = ffi.C.getsockopt(self.fh,
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
    local len = 2 * box.socket.internal.INT_SIZE
    local res = ffi.C.setsockopt(self.fh,
        box.socket.internal.SOL_SOCKET, info.iname, value, len)
    if res < 0 then
        self._errno = box.errno()
        return nil
    end

    return active, timeout
end


socket_methods.accept = function(self)

    self._errno = nil

    local fh = ffi.C.accept(self.fh, nil, nil)

    if fh < 1 then
        self._errno = box.errno()
        return nil
    end

    local socket = { fh = fh }
    setmetatable(socket, box.socket.internal.socket_mt)
    return socket
end


socket_methods.read = function(self, size, timeout)
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    if self.rbuf == nil then
        self.rbuf = ''
    end
    
    if size == nil then
        size = 4294967295
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
            self._errno = box.errno()
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
    for i, eol in pairs(eols) do
        if string.len(rbuf) >= string.len(eol) then
            local data = string.match(rbuf, "^(.*" .. eol .. ")")
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

socket_methods.readline = function(self, limit, eol, timeout)

    if type(limit) == 'table' then -- :readline({eol}[, timeout ])
        if eol ~= nil then
            timeout = eol
            eol = limit
            limit = nil
        else
            eol = limit
            limit = nil
        end
    elseif eol ~= nil then
        if type(eol) ~= 'table' then
            eol = { eol }
        end
    end
    
    if timeout == nil then
        timeout = TIMEOUT_INFINITY
    end

    if self.rbuf == nil then
        self.rbuf = ''
    end

    if limit == nil then
        limit = 4294967295
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
            if string.len(self.rbuf) >= limit then
               data = string.sub(self.rbuf, 1, limit)
               self.rbuf = string.sub(self.rbuf, limit + 1)
               return data
            end

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
        elseif self:errno() ~= box.errno.EAGAIN then
            self._errno = box.errno()
            return nil
        end
    end
end

socket_methods.write = function(self, octets, timeout)
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
    local iflags = get_iflags(box.socket.internal.SEND_FLAGS, flags)

    self._errno = nil
    local res = ffi.C.send(self.fh, octets, string.len(octets), iflags)
    if res == -1 then
        self._errno = box.errno()
        return false
    end
    return true
end

socket_methods.recv = function(self, size, flags)
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

    local res = ffi.C.recv(self.fh, buf, size, iflags)

    if res == -1 then
        self._errno = box.errno()
        return nil
    end
    return ffi.string(buf, res)
end


socket_methods.recvfrom = function(self, size, flags)
    local iflags = get_iflags(box.socket.internal.SEND_FLAGS, flags)
    if iflags == nil then
        self._errno = box.errno.EINVAL
        return nil
    end
    self._errno = nil
    local res, from = box.socket.internal.recvfrom(self.fh, size, iflags)
    if res == nil then
        self._errno = box.errno()
        return nil
    end
    return res, from
end

socket_methods.sendto = function(self, host, port, octets, flags)
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

    local res = ffi.C.bsdsocket_sendto(self.fh, host, port,
        octets, string.len(octets), iflags)

    if res < 0 then
        self._errno = box.errno()
        return false
    end
    return true
end


local function create_socket(self, domain, stype, proto)

    self._errno = nil

    local idomain = get_ivalue(box.socket.internal.DOMAIN, domain)
    if idomain == nil then
        self._errno = box.errno.EINVAL
        return nil
    end

    local itype = get_ivalue(box.socket.internal.SO_TYPE, stype)
    if itype == nil then
        self._errno = box.errno.EINVAL
        return nil
    end

    local iproto = ffi.C.bsdsocket_protocol(proto)
    if iproto == -1 then
        self._errno = box.errno()
        return nil
    end


    local fh = ffi.C.socket(idomain, itype, iproto)
    if fh < 0 then
        self._errno = box.errno()
        return nil
    end

    local socket = { fh = fh }
    setmetatable(socket, box.socket.internal.socket_mt)
    return socket
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
            local iproto = ffi.C.bsdsocket_protocol(opts.protocol)
            if iproto == -1 then
                self._errno = box.errno()
                return nil
            end
            ga_opts.protocol = iproto
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

socket_methods.name = function(self)
    local aka = box.socket.internal.name(self.fh)
    if aka == nil then
        self._errno = box.errno()
        return nil
    end
    return aka
end

if type(box.socket) == nil then
    box.socket = {}
end

box.socket.internal = {
    socket_mt   = {
        __index     = socket_methods,
        __tostring  = function(self)
            local name = sprintf("fd %d", self.fh)
            local aka = box.socket.internal.name(self.fh)
            if aka ~= nil then
                name = sprintf("%s, aka %s:%s", name, aka.host, aka.port)
            end
            local peer = box.socket.internal.peer(self.fh)
            if peer ~= nil then
                name = sprintf("%s, peer %s:%s", name, peer.host, peer.port)
            end
            return name
        end
    },
    
}

setmetatable(box.socket, {
    __call = create_socket,
    __index = {
        errno = errno,
        error = errstr,
        getaddrinfo = getaddrinfo,
    }
})

end
