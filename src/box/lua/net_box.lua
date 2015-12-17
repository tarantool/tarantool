-- net_box.lua (internal file)

local internal = require 'net.box.lib'
local msgpack = require 'msgpack'
local fiber = require 'fiber'
local socket = require 'socket'
local log = require 'log'
local errno = require 'errno'
local ffi = require 'ffi'
local yaml = require 'yaml'
local urilib = require 'uri'
local buffer = require 'buffer'

-- packet codes
local OK                = 0
local SELECT            = 1
local INSERT            = 2
local REPLACE           = 3
local UPDATE            = 4
local DELETE            = 5
local CALL              = 6
local AUTH              = 7
local EVAL              = 8
local UPSERT            = 9
local PING              = 64
local ERROR_TYPE        = 65536

-- packet keys
local TYPE              = 0x00
local SYNC              = 0x01
local SCHEMA_ID         = 0x05
local SPACE_ID          = 0x10
local INDEX_ID          = 0x11
local LIMIT             = 0x12
local OFFSET            = 0x13
local ITERATOR          = 0x14
local INDEX_BASE        = 0x15
local KEY               = 0x20
local TUPLE             = 0x21
local FUNCTION_NAME     = 0x22
local USER              = 0x23
local EXPR              = 0x27
local OPS               = 0x28
local DATA              = 0x30
local ERROR             = 0x31
local GREETING_SIZE     = 128

local SPACE_SPACE_ID    = 280
local SPACE_INDEX_ID    = 288

local TIMEOUT_INFINITY  = 500 * 365 * 86400

local sequence_mt = { __serialize = 'sequence' }
local mapping_mt = { __serialize = 'mapping' }

local CONSOLE_FAKESYNC  = 15121974
local CONSOLE_DELIMITER = "$EOF$"
local ER_WRONG_SCHEMA_VERSION = 109

ffi.cdef [[
enum {
    /* Maximal length of protocol name in handshake */
    GREETING_PROTOCOL_LEN_MAX = 32,
    /* Maximal length of salt in handshake */
    GREETING_SALT_LEN_MAX = 44,
};

struct greeting {
    uint32_t version_id;
    uint32_t salt_len;
    char protocol[GREETING_PROTOCOL_LEN_MAX + 1];
    struct tt_uuid uuid;
    char salt[GREETING_SALT_LEN_MAX];
};

int
greeting_decode(const char *greetingbuf, struct greeting *greeting);
int strcmp(const char *s1, const char *s2);
]]
local builtin = ffi.C

local ch_buf = {}
local ch_buf_size = 0

local function get_channel()
    if ch_buf_size == 0 then
        return fiber.channel()
    end

    local ch = ch_buf[ch_buf_size]
    ch_buf[ch_buf_size] = nil
    ch_buf_size = ch_buf_size - 1
    return ch
end

local function free_channel(ch)
    -- return channel to buffer
    ch_buf[ch_buf_size + 1] = ch
    ch_buf_size = ch_buf_size + 1
end

local function one_tuple(tbl)
    if tbl == nil then
        return
    end
    if #tbl > 0 then
        return tbl[1]
    end
    return
end

local requests = {
    [PING]    = internal.encode_ping;
    [AUTH]    = internal.encode_auth;
    [CALL]    = internal.encode_call;
    [EVAL]    = internal.encode_eval;
    [INSERT]  = internal.encode_insert;
    [REPLACE] = internal.encode_replace;
    [DELETE] = internal.encode_delete;
    [UPDATE]  = internal.encode_update;
    [UPSERT]  = internal.encode_upsert;
    [SELECT]  = function(wbuf, sync, schema_id, spaceno, indexno, key, opts)
        if opts == nil then
            opts = {}
        end
        if spaceno == nil or type(spaceno) ~= 'number' then
            box.error(box.error.NO_SUCH_SPACE, '#'..tostring(spaceno))
        end

        if indexno == nil or type(indexno) ~= 'number' then
            box.error(box.error.NO_SUCH_INDEX, indexno, '#'..tostring(spaceno))
        end

        local limit, offset
        if opts.limit ~= nil then
            limit = tonumber(opts.limit)
        else
            limit = 0xFFFFFFFF
        end
        if opts.offset ~= nil then
            offset = tonumber(opts.offset)
        else
            offset = 0
        end
        local iterator = require('box.internal').check_iterator_type(opts,
            key == nil or (type(key) == 'table' and #key == 0))

        internal.encode_select(wbuf, sync, schema_id, spaceno, indexno,
            iterator, offset, limit, key)
    end;
}

local function check_if_space(space)
    if type(space) == 'table' and space.id ~= nil then
        return
    end
    error("Use space:method(...) instead space.method(...)")
end

local function space_metatable(self)
    return {
        __index = {
            insert  = function(space, tuple)
                check_if_space(space)
                return self:_insert(space.id, tuple)
            end,

            replace = function(space, tuple)
                check_if_space(space)
                return self:_replace(space.id, tuple)
            end,

            select = function(space, key, opts)
                check_if_space(space)
                return self:_select(space.id, 0, key, opts)
            end,

            delete = function(space, key)
                check_if_space(space)
                return self:_delete(space.id, key, 0)
            end,

            update = function(space, key, oplist)
                check_if_space(space)
                return self:_update(space.id, key, oplist, 0)
            end,

            upsert = function(space, tuple_key, oplist)
                check_if_space(space)
                return self:_upsert(space.id, tuple_key, oplist, 0)
            end,

            get = function(space, key)
                check_if_space(space)
                local res = self:_select(space.id, 0, key,
                                    { limit = 2, iterator = 'EQ' })
                if #res == 0 then
                    return
                end
                if #res == 1 then
                    return res[1]
                end
                box.error(box.error.MORE_THAN_ONE_TUPLE)
            end
        }
    }
end

local function check_if_index(idx)
    if type(idx) == 'table' and idx.id ~= nil and type(idx.space) == 'table' then
        return
    end
    error('Use index:method(...) instead index.method(...)')
end

local function index_metatable(self)
    return {
        __index = {
            select = function(idx, key, opts)
                check_if_index(idx)
                return self:_select(idx.space.id, idx.id, key, opts)
            end,

            get = function(idx, key)
                check_if_index(idx)
                local res = self:_select(idx.space.id, idx.id, key,
                                    { limit = 2, iterator = 'EQ' })
                if #res == 0 then
                    return
                end
                if #res == 1 then
                    return res[1]
                end
                box.error(box.error.MORE_THAN_ONE_TUPLE)
            end,

            min = function(idx, key)
                check_if_index(idx)
                local res = self:_select(idx.space.id, idx.id, key,
                    { limit = 1, iterator = 'GE' })
                if #res > 0 then
                    return res[1]
                end
            end,

            max = function(idx, key)
                check_if_index(idx)
                local res = self:_select(idx.space.id, idx.id, key,
                    { limit = 1, iterator = 'LE' })
                if #res > 0 then
                    return res[1]
                end
            end,

            count = function(idx, key)
                check_if_index(idx)
                local proc = string.format('box.space.%s.index.%s:count',
                    idx.space.name, idx.name)
                local res = self:call(proc, key)
                if #res > 0 then
                    return res[1][1]
                end
            end,

            delete = function(idx, key)
                check_if_index(idx)
                return self:_delete(idx.space.id, key, idx.id)
            end,

            update = function(idx, key, oplist)
                check_if_index(idx)
                return self:_update(idx.space.id, key, oplist, idx.id)
            end,

            upsert = function(idx, tuple_key, oplist)
                check_if_index(idx)
                return self:_upsert(idx.space.id, tuple_key, oplist, idx.id)
            end,

        }
    }
end

local errno_is_transient = {
    [errno.EAGAIN] = true;
    [errno.EWOULDBLOCK] = true;
    [errno.EINTR] = true;
}

local remote = {}

local remote_methods = {
    new = function(cls, host, port, opts)
        local self = { _sync = -1 }

        if type(cls) == 'table' then
            setmetatable(self, getmetatable(cls))
        else
            host, port, opts = cls, host, port
            setmetatable(self, getmetatable(remote))
        end


        -- uri as the first argument
        if opts == nil then
            opts = {}
            if type(port) == 'table' then
                opts = port
                port = nil
            end

            if port == nil then

                local address = urilib.parse(tostring(host))
                if address == nil or address.service == nil then
                    box.error(box.error.PROC_LUA,
                        "usage: remote:new(uri[, opts] | host, port[, opts])")
                end

                host = address.host
                port = address.service

                opts.user = address.login or opts.user
                opts.password = address.password or opts.password
            end
        end


        self.is_instance = true
        self.host = host
        self.port = port
        self.opts = opts

        if self.opts == nil then
            self.opts = {}
        end

        if self.opts.user ~= nil and self.opts.password == nil then
            box.error(box.error.PROC_LUA,
                "net.box: password is not defined")
        end
        if self.opts.user == nil and self.opts.password ~= nil then
            box.error(box.error.PROC_LUA,
                "net.box: user is not defined")
        end


        if self.host == nil then
            self.host = 'localhost'
        end

        self.is_run = true
        self.state = 'init'
        self.wbuf = buffer.ibuf(buffer.READAHEAD)
        self.rpos = 1
        self.rlen = 0

        self.ch = { sync = {}, fid = {} }
        self.wait = { state = {} }
        self.timeouts = {}

        fiber.create(function() self:_connect_worker() end)
        fiber.create(function() self:_read_worker() end)
        fiber.create(function() self:_write_worker() end)

        if self.opts.wait_connected == nil or self.opts.wait_connected then
            self:wait_connected()
        end

        return self
    end,

    -- sync
    sync    = function(self)
        self._sync = self._sync + 1
        if self._sync >= 0x7FFFFFFF then
            self._sync = 0
        end
        return self._sync
    end,

    ping    = function(self)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:ping()")
        end
        if not self:is_connected() then
            return false
        end
        local res = self:_request(PING, false)


        if res == nil then
            return false
        end

        if res.hdr[TYPE] == OK then
            return true
        end
        return false
    end,

    _console = function(self, line)
        local data = line..CONSOLE_DELIMITER.."\n\n"
        ffi.copy(self.wbuf.wpos, data, #data)
        self.wbuf.wpos = self.wbuf.wpos + #data

        local res = self:_request_raw(EVAL, CONSOLE_FAKESYNC, data, true)
        return res.body[DATA]
    end,

    call    = function(self, proc_name, ...)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:call(proc_name, ...)")
        end

        proc_name = tostring(proc_name)

        local res = self:_request(CALL, true, proc_name, {...})
        return res.body[DATA]

    end,

    eval    = function(self, expr, ...)
        if type(self) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:eval(expr, ...)")
        end

        expr = tostring(expr)
        local data = self:_request(EVAL, true, expr, {...}).body[DATA]
        local data_len = #data
        if data_len == 1 then
            return data[1]
        elseif data_len == 0 then
            return
        else
            return unpack(data)
        end
    end,

    is_connected = function(self)
        return self.state == 'active' or self.state == 'activew'
    end,

    wait_connected = function(self, timeout)
        return self:_wait_state(self._request_states, timeout)
    end,

    timeout = function(self, timeout)
        if timeout == nil then
            return self
        end
        if self.is_instance then
            self.timeouts[ fiber.id() ] = timeout
            return self
        end

        return {
            new = function(cls, host, port, opts)
                if opts == nil then
                    opts = {}
                end

                opts.wait_connected = false

                local cn = self:new(host, port, opts)

                if not cn:wait_connected(timeout) then
                    cn:close()
                    box.error(box.error.TIMEOUT)
                end
                return cn
            end
        }
    end,

    reload_schema = function(self)
         self._schema_id = nil
         self:_load_schema()
         if self.state ~= 'error' and self.state ~= 'closed' then
               self:_switch_state('active')
         end
    end,

    close = function(self)
        if self.state ~= 'closed' then
            self:_switch_state('closed')
            self:_error_waiters('Connection was closed')
            if self.s ~= nil then
                self.s:close()
                self.s = nil
            end
        end
    end,

    -- private methods
    _fatal = function(self, efmt, ...)
        if self.state == 'error' then
            return
        end
        local emsg = efmt
        if select('#', ...) > 0 then
            emsg = string.format(efmt, ...)
        end

        if self.s ~= nil then
            self.s:close()
            self.s = nil
        end

        log.warn("%s:%s: %s", self.host or "", self.port or "", tostring(emsg))
        self.error = emsg
        self.space = {}
        self:_switch_state('error')
        self:_error_waiters(emsg)
        self.rpos = 1
        self.rlen = 0
        self.version_id = nil
        self.version = nil
        self.salt = nil
    end,

    _wakeup_client = function(self, hdr, body)
        local sync = hdr[SYNC]

        local ch = self.ch.sync[sync]
        if ch ~= nil then
            ch.response = { hdr = hdr, body = body }
            fiber.wakeup(ch.fid)
        else
            log.warn("Unexpected response %s", tostring(sync))
        end
    end,

    _error_waiters = function(self, emsg)
        local waiters = self.ch.sync
        self.ch.sync = {}
        for sync, channel in pairs(waiters) do
            channel.response = {
                hdr = {
                    [TYPE] = bit.bor(ERROR_TYPE, box.error.NO_CONNECTION),
                    [SYNC] = sync
                },
                body = {
                    [ERROR] = emsg
                }
            }
            fiber.wakeup(channel.fid)
        end
    end,

    _check_console_response = function(self)
        local docend = "\n...\n"
        local resp = ffi.string(self.rbuf.rpos, self.rbuf:size())

        if #resp < #docend or
                string.sub(resp, #resp + 1 - #docend) ~= docend then
            return -1
        end

        local hdr = { [SYNC] = CONSOLE_FAKESYNC, [TYPE] = 0 }
        local body = { [DATA] = resp }

        self:_wakeup_client(hdr, body)
        self.rbuf:read(#resp)
        return 0
    end,

    _check_binary_response = function(self)
        while true do
            if self.rbuf.rpos + 5 > self.rbuf.wpos then
                return 5 - (self.rbuf.wpos - self.rbuf.rpos)
            end

            local rpos, len = msgpack.ibuf_decode(self.rbuf.rpos)
            if rpos + len > self.rbuf.wpos then
                return len - (self.rbuf.wpos - rpos)
            end

            local rpos, hdr = msgpack.ibuf_decode(rpos)
            self._schema_id = hdr[SCHEMA_ID]
            local body = {}

            if rpos < self.rbuf.wpos then
                rpos, body= msgpack.ibuf_decode(rpos)
            end
            setmetatable(body, mapping_mt)

            self.rbuf.rpos = rpos
            self:_wakeup_client(hdr, body)

           if self.rbuf:size() == 0 then
                return 0
            end
        end
    end,

    _switch_state = function(self, state)
        if self.state == state or state == nil then
            return
        end
        self.state = state

        local list = self.wait.state[ state ] or {}
        self.wait.state[ state ] = {}

        for _, fid in pairs(list) do
            if fid ~= fiber.id() then
                if self.ch.fid[fid] ~= nil then
                    self.ch.fid[fid]:put(true)
                    self.ch.fid[fid] = nil
                end
            end
        end
    end,

    _wait_state = function(self, states, timeout)
        timeout = timeout or TIMEOUT_INFINITY
        while timeout > 0 and self:_is_state(states) ~= true do
            local started = fiber.time()
            local fid = fiber.id()
            local ch = get_channel()
            for state, _ in pairs(states) do
                self.wait.state[state] = self.wait.state[state] or {}
                self.wait.state[state][fid] = fid
            end

            self.ch.fid[fid] = ch
            ch:get(timeout)
            self.ch.fid[fid] = nil
            free_channel(ch)

            for state, _ in pairs(states) do
                self.wait.state[state][fid] = nil
            end
            timeout = timeout - (fiber.time() - started)
        end
        return self.state
    end,

    _connect_worker = function(self)
        fiber.name('net.box.connector')
        local connect_states = { init = true, error = true, closed = true }
        while self:_wait_state(connect_states) ~= 'closed' do

            if self.state == 'error' then
                if self.opts.reconnect_after == nil then
                    self:_switch_state('closed')
                    return
                end
                fiber.sleep(self.opts.reconnect_after)
            end

            self:_switch_state('connecting')

            self.s = socket.tcp_connect(self.host, self.port)
            if self.s == nil then
                self:_fatal(errno.strerror(errno()))
            else

                -- on_connect
                self:_switch_state('handshake')
                local greetingbuf = self.s:read(GREETING_SIZE)
                if greetingbuf == nil then
                    self:_fatal(errno.strerror(errno()))
                elseif #greetingbuf ~= GREETING_SIZE then
                    self:_fatal("Can't read handshake")
                else
                    local greeting = ffi.new('struct greeting')
                    if builtin.greeting_decode(greetingbuf, greeting) ~= 0 then
                        self:_fatal("Can't decode handshake")
                    elseif builtin.strcmp(greeting.protocol, 'Lua console') == 0 then
                        self.version_id = greeting.version_id
                        -- enable self:console() method
                        self.console = self._console
                        self._check_response = self._check_console_response
                        -- set delimiter
                        self:_switch_state('schema')

                        local line = "require('console').delimiter('"..CONSOLE_DELIMITER.."')\n\n"
                        ffi.copy(self.wbuf.wpos, line, #line)
                        self.wbuf.wpos = self.wbuf.wpos + #line

                        local res = self:_request_raw(EVAL, CONSOLE_FAKESYNC, line, true)

                        if res.hdr[TYPE] ~= OK then
                            self:_fatal(res.body[ERROR])
                        end
                        self:_switch_state('active')
                    elseif builtin.strcmp(greeting.protocol, 'Binary') == 0 then
                        self.version_id = greeting.version_id
                        self.salt = ffi.string(greeting.salt, greeting.salt_len)
                        self.console = nil
                        self._check_response = self._check_binary_response
                        local s, e = pcall(function()
                            self:_auth()
                        end)
                        if not s then
                            self:_fatal(e)
                        end

                        xpcall(function() self:_load_schema() end,
                            function(e)
                                log.error("Can't load schema: %s", tostring(e))
                            end)

                        if self.state ~= 'error' and self.state ~= 'closed' then
                            self:_switch_state('active')
                        end
                    else
                        self:_fatal("Unsupported protocol - "..
                            ffi.string(greeting.protocol))
                    end
                end
            end
        end
    end,

    _auth = function(self)
        if self.opts.user == nil or self.opts.password == nil then
            return
        end

        self:_switch_state('auth')

        local auth_res = self:_request_internal(AUTH,
            false, self.opts.user, self.opts.password, self.salt)

        if auth_res.hdr[TYPE] ~= OK then
            self:_fatal(auth_res.body[ERROR])
        end
    end,

    -- states wakeup _read_worker
    _r_states = {
        active = true, activew = true, schema = true,
        schemaw = true, auth = true, authw = true,
        closed = true,
    },
    -- states wakeup _write_worker
    _rw_states = {
        activew = true, schemaw = true, authw = true,
        closed = true,
    },
    _request_states = {
        active = true, activew = true, closed = true,
    },

    _is_state = function(self, states)
        return states[self.state] ~= nil
    end,

    _is_r_state = function(self)
        return is_state(self._r_states)
    end,

    _is_rw_state = function(self)
        return is_state(self._rw_states)
    end,

    _load_schema = function(self)
        if self.state == 'closed' or self.state == 'error' then
            self:_fatal('Can not load schema from the state')
            return
        end

        self:_switch_state('schema')

        -- FIXME: box.schema.VSPACE_ID
        local spaces = self:_request_internal(SELECT,
            true, SPACE_SPACE_ID, 0, nil, { iterator = 'ALL' }).body[DATA]
        local indexes = self:_request_internal(SELECT,
            true, SPACE_INDEX_ID, 0, nil, { iterator = 'ALL' }).body[DATA]

        local sl = {}

        for _, space in pairs(spaces) do
            local name = space[3]
            local id = space[1]
            local engine = space[4]
            local field_count = space[5]

            local s = {
                id              = id,
                name            = name,
                engine          = engine,
                field_count     = field_count,
                enabled         = true,
                index           = {}
            }
            if #space > 5 and string.match(space[6], 'temporary') then
                s.temporary = true
            else
                s.temporary = false
            end

            setmetatable(s, space_metatable(self))

            sl[id] = s
            sl[name] = s
        end

        for _, index in pairs(indexes) do
            local idx = {
                space   = index[1],
                id      = index[2],
                name    = index[3],
                type    = string.upper(index[4]),
                parts   = {},
            }
            local OPTS = 5
            local PARTS = 6

            if type(index[OPTS]) == 'number' then
                idx.unique = index[OPTS] == 1 and true or false

                for k = 0, index[PARTS] - 1 do
                    local pktype = index[7 + k * 2 + 1]
                    local pkfield = index[7 + k * 2]

                    local pk = {
                        type = string.upper(pktype),
                        fieldno = pkfield
                    }
                    idx.parts[k] = pk
                end
            else
                for k = 1, #index[PARTS] do
                    local pktype = index[PARTS][k][2]
                    local pkfield = index[PARTS][k][1]

                    local pk = {
                        type = string.upper(pktype),
                        fieldno = pkfield
                    }
                    idx.parts[k - 1] = pk
                end
                idx.unique = index[OPTS].is_unique and true or false
            end

            if sl[idx.space] ~= nil then
                sl[idx.space].index[idx.id] = idx
                sl[idx.space].index[idx.name] = idx
                idx.space = sl[idx.space]
                setmetatable(idx, index_metatable(self))
            end
        end

        self.space = sl
    end,

    _read_worker = function(self)
        fiber.name('net.box.read')
        self.rbuf = buffer.ibuf(buffer.READAHEAD)
        while self:_wait_state(self._r_states) ~= 'closed' do
            if self.s:readable() then
                local len = self.s:sysread(self.rbuf.wpos, self.rbuf:unused())
                if len ~= nil then
                    if len == 0 then
                        self:_fatal('Remote host closed connection')
                    else
                        self.rbuf.wpos = self.rbuf.wpos + len

                        local advance = self:_check_response()
                        if advance <= 0 then
                            advance = buffer.READAHEAD
                        end

                        self.rbuf:reserve(advance)
                    end
                elseif errno_is_transient[errno()] ~= true then
                    self:_fatal(errno.strerror(errno()))
                end
            end
        end
    end,

    _to_wstate = { active = 'activew', schema = 'schemaw', auth = 'authw' },
    _to_rstate = { activew = 'active', schemaw = 'schema', authw = 'auth' },

    _write_worker = function(self)
        fiber.name('net.box.write')
        while self:_wait_state(self._rw_states) ~= 'closed' do
            while self.wbuf:size() > 0 do
                local written = self.s:syswrite(self.wbuf.rpos, self.wbuf:size())
                if written ~= nil then
                    self.wbuf.rpos = self.wbuf.rpos + written
                else
                    if errno_is_transient[errno()] then
                    -- the write is with a timeout to detect FIN
                    -- packet on the receiving end, and close the connection.
                    -- Every second sockets we iterate the while loop
                    -- and check the connection state
                        while self.s:writable(1) == 0 and self.state ~= 'closed' do
                        end
                    else
                        self:_fatal(errno.strerror(errno()))
                        break
                    end
                end
            end
            self.wbuf:reserve(buffer.READAHEAD)
            self:_switch_state(self._to_rstate[self.state])
        end
    end,

    _request = function(self, reqtype, raise, ...)
        if self.console then
            box.error(box.error.UNSUPPORTED, "console", reqtype)
        end
        local fid = fiber.id()
        if self.timeouts[fid] == nil then
            self.timeouts[fid] = TIMEOUT_INFINITY
        end

        local started = fiber.time()

        self:_wait_state(self._request_states, self.timeouts[fid])

        self.timeouts[fid] = self.timeouts[fid] - (fiber.time() - started)

        if self.state == 'closed' then
            if raise then
                box.error(box.error.NO_CONNECTION)
            end
        end

        if self.timeouts[fid] <= 0 then
            self.timeouts[fid] = nil
            if raise then
                box.error(box.error.TIMEOUT)
            else
                return {
                    hdr = { [TYPE] = bit.bor(ERROR_TYPE, box.error.TIMEOUT) },
                    body = { [ERROR] = 'Timeout exceeded' }
                }
            end
        end

        return self:_request_internal(reqtype, raise, ...)
    end,

    _request_raw = function(self, reqtype, sync, request, raise)

        local fid = fiber.id()
        if self.timeouts[fid] == nil then
            self.timeouts[fid] = TIMEOUT_INFINITY
        end

        self:_switch_state(self._to_wstate[self.state])

        local ch = { fid = fid; }
        self.ch.sync[sync] = ch
        fiber.sleep(self.timeouts[fid])
        local response = ch.response
        self.ch.sync[sync] = nil
        self.timeouts[fid] = nil

        if response == nil then
            if raise then
                box.error(box.error.TIMEOUT)
            else
                return {
                    hdr = { [TYPE] = bit.bor(ERROR_TYPE, box.error.TIMEOUT) },
                    body = { [ERROR] = 'Timeout exceeded' }
                }
            end
        end


        if response.body[DATA] ~= nil and reqtype ~= EVAL then
            if rawget(box, 'tuple') ~= nil then
                for i, v in pairs(response.body[DATA]) do
                    response.body[DATA][i] =
                        box.tuple.new(response.body[DATA][i])
                end
            end
            -- disable YAML flow output (useful for admin console)
            setmetatable(response.body[DATA], sequence_mt)
        end

        return response
    end,

    _request_internal = function(self, reqtype, raise, ...)
        local sync = self:sync()
        local schema_id = 0
        if self._schema_id ~= nil then
            schema_id = self._schema_id
        end
        local request = requests[reqtype](self.wbuf, sync, schema_id, ...)
        local response =  self:_request_raw(reqtype, sync, request, raise)
        local resptype = response.hdr[TYPE]
        if resptype ~= OK then
            local err_code = bit.band(resptype, bit.lshift(1, 15) - 1)
            -- handle expired schema:
            -- reload schema and return error
            if err_code == ER_WRONG_SCHEMA_VERSION then
                self._schema_id = nil
                self:reload_schema()
                return self:_request_internal(reqtype, raise, ...)
            elseif raise then
                box.error({
                    code = err_code,
                    reason = response.body[ERROR]
                })
            end
        end
        return response
    end,

    -- private (low level) methods
    _select = function(self, spaceno, indexno, key, opts)
        local res = self:_request(SELECT, true, spaceno, indexno, key, opts)
        return res.body[DATA]
    end,

    _insert = function(self, spaceno, tuple)
        local res = self:_request(INSERT, true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    _replace = function(self, spaceno, tuple)
        local res = self:_request(REPLACE, true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    _delete  = function(self, spaceno, key, index_id)
        local res = self:_request(DELETE, true, spaceno, index_id, key, index_id)
        return one_tuple(res.body[DATA])
    end,

    _update = function(self, spaceno, key, oplist, index_id)
        local res = self:_request(UPDATE, true, spaceno, index_id, key, oplist)
        return one_tuple(res.body[DATA])
    end,

    _upsert = function(self, spaceno, tuple_key, oplist, index_id)
        local res = self:_request(UPSERT, true, spaceno,
                                  index_id, tuple_key, oplist)
        return one_tuple(res.body[DATA])
    end,
}

setmetatable(remote, { __index = remote_methods })

local function rollback()
    if rawget(box, 'rollback') ~= nil then
        -- roll back local transaction on error
        box.rollback()
    end
end

local function handle_call_result(status, ...)
    if not status then
        rollback()
        return box.error(box.error.PROC_LUA, (...))
    end
    if select('#', ...) == 1 and type((...)) == 'table' then
        local result = (...)
        for i, v in pairs(result) do
            result[i] = box.tuple.new(v)
        end
        return result
    else
        local result = {}
        for i=1,select('#', ...), 1 do
            result[i] = box.tuple.new((select(i, ...)))
        end
        return result
    end
end

local function handle_eval_result(status, ...)
    if not status then
        rollback()
        return box.error(box.error.PROC_LUA, (...))
    end
    return ...
end

remote.self = {
    ping = function() return true end,
    reload_schema = function() end,
    close = function() end,
    timeout = function(self) return self end,
    wait_connected = function(self) return true end,
    is_connected = function(self) return true end,
    call = function(_box, proc_name, ...)
        if type(_box) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:call(proc_name, ...)")
        end
        proc_name = tostring(proc_name)
        local status, proc, obj = pcall(package.loaded['box.internal'].
            call_loadproc, proc_name)
        if not status then
            rollback()
            return box.error() -- re-throw
        end
        local result
        if obj ~= nil then
            return handle_call_result(pcall(proc, obj, ...))
        else
            return handle_call_result(pcall(proc, ...))
        end
    end,
    eval = function(_box, expr, ...)
        if type(_box) ~= 'table' then
            box.error(box.error.PROC_LUA, "usage: remote:eval(expr, ...)")
        end
        local proc, errmsg = loadstring(expr)
        if not proc then
            proc, errmsg = loadstring("return "..expr)
        end
        if not proc then
            rollback()
            return box.error(box.error.PROC_LUA, errmsg)
        end
        return handle_eval_result(pcall(proc, ...))
    end
}

setmetatable(remote.self, {
    __index = function(self, key)
        if key == 'space' then
            -- proxy self.space to box.space
            return require('box').space
        end
        return nil
    end
})

package.loaded['net.box'] = remote
