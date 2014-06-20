-- net_box.lua (internal file)

local msgpack = require 'msgpack'
local fiber = require 'fiber'
local socket = require 'socket'
local log = require 'log'
local errno = require 'errno'
local ffi = require 'ffi'
local digest = require 'digest'
local yaml = require 'yaml'

local CODE              = 0
local PING              = 64
local SELECT            = 1
local INSERT            = 2
local REPLACE           = 3
local UPDATE            = 4
local DELETE            = 5
local CALL              = 6
local AUTH              = 7
local TYPE              = 0x00
local SYNC              = 0x01
local SPACE_ID          = 0x10
local INDEX_ID          = 0x11
local LIMIT             = 0x12
local OFFSET            = 0x13
local ITERATOR          = 0x14
local KEY               = 0x20
local TUPLE             = 0x21
local FUNCTION_NAME     = 0x22
local USER              = 0x23
local DATA              = 0x30
local ERROR             = 0x31
local GREETING_SIZE     = 128

local TIMEOUT_INFINITY  = 500 * 365 * 86400


ffi.cdef[[
    int base64_decode(const char *in_base64, int in_len,
                  char *out_bin, int out_len);
]]


local function request(header, body)

    header = msgpack.encode(header)
    body = msgpack.encode(body)

    local len = msgpack.encode(string.len(header) + string.len(body))


    return len .. header .. body
end


local function strxor(s1, s2)
    local res = ''
    for i = 1, string.len(s1) do
        if i > string.len(s2) then
            break
        end

        local b1 = string.byte(s1, i)
        local b2 = string.byte(s2, i)
        res = res .. string.char(bit.bxor(b1, b2))
    end
    return res
end

local function b64decode(str)
    local so = ffi.new('char[?]', string.len(str) * 2);
    local len =
        ffi.C.base64_decode(str, string.len(str), so, string.len(str) * 2)
    return ffi.string(so, len)
end


local function keyfy(v)
    if type(v) == 'table' then
        return v
    end
    if v == nil then
        return {}
    end
    return { v }
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

local proto = {
    _sync = -1,

    -- sync
    sync    = function(self)
        self._sync = self._sync + 1
        if self._sync >= 0x7FFFFFFF then
            self._sync = 0
        end
        return self._sync
    end,


    ping    = function(sync)
        return request(
            { [SYNC] = sync, [TYPE] = PING },
            {}
        )
    end,


    -- lua call
    call = function(sync, proc, args)
        if args == nil then
            args = {}
        end
        return request(
            { [SYNC] = sync, [TYPE] = CALL  },
            { [FUNCTION_NAME] = proc, [TUPLE] = args }
        )
    end,

    -- insert
    insert = function(sync, spaceno, tuple)
        return request(
            { [SYNC] = sync, [TYPE] = INSERT },
            { [SPACE_ID] = spaceno, [TUPLE] = tuple }
        )
    end,

    -- replace
    replace = function(sync, spaceno, tuple)
        return request(
            { [SYNC] = sync, [TYPE] = REPLACE },
            { [SPACE_ID] = spaceno, [TUPLE] = tuple }
        )
    end,

    -- delete
    delete = function(sync, spaceno, key)
        return request(
            { [SYNC] = sync, [TYPE] = DELETE },
            { [SPACE_ID] = spaceno, [KEY] = key }
        )
    end,

    -- update
    update = function(sync, spaceno, key, oplist)
        return request(
            { [SYNC] = sync, [TYPE] = UPDATE },
            { [KEY] = key,   [TUPLE]  = oplist, [SPACE_ID] = spaceno }
        )
    end,

    -- select
    select  = function(sync, spaceno, indexno, key, opts)

        if opts == nil then
            opts = {}
        end
        if spaceno == nil or type(spaceno) ~= 'number' then
            box.raise(box.error.NO_SUCH_SPACE,
                string.format("Space %s does not exist", tostring(spaceno)))
        end

        if indexno == nil or type(indexno) ~= 'number' then
            box.raise(box.error.NO_SUCH_INDEX,
                string.format("No index #%s is defined in space %s",
                    tostring(spaceno),
                    tostring(indexno)))
        end

        local body = {
            [SPACE_ID] = spaceno,
            [INDEX_ID] = indexno,
            [KEY] = keyfy(key)
        }

        if opts.limit ~= nil then
            body[LIMIT] = tonumber(opts.limit)
        else
            body[LIMIT] = 0xFFFFFFFF
        end
        if opts.offset ~= nil then
            body[OFFSET] = tonumber(opts.offset)
        else
            body[OFFSET] = 0
        end

        if opts.iterator ~= nil then
            if type(opts.iterator) == 'string' then
                local iterator = box.index[ opts.iterator ]
                if iterator == nil then
                    box.raise(box.error.INVALID_MSGPACK,
                        "Wrong iterator " .. opts.iterator)
                end
                body[ITERATOR] = iterator
            else
                body[ITERATOR] = tonumber(opts.iterator)
            end
        end

        return request( { [SYNC] = sync, [TYPE] = SELECT }, body )
    end,

    auth = function(sync, user, password, handshake)
        local saltb64 = string.sub(handshake, 65)
        local salt = string.sub(b64decode(saltb64), 1, 20)

        local hpassword = digest.sha1(password)
        local hhpassword = digest.sha1(hpassword)
        local scramble = digest.sha1(salt .. hhpassword)

        local hash = strxor(hpassword, scramble)
        return request(
            { [SYNC] = sync, [TYPE] = AUTH },
            { [USER] = user, [TUPLE] = { 'chap-sha1', hash } }
        )
    end,

    b64decode = b64decode,
}


local function space_metatable(self)
    return {
        __index = {
            insert  = function(space, tuple)
                return self:_insert(space.id, tuple)
            end,

            replace = function(space, tuple)
                return self:_replace(space.id, tuple)
            end,

            select = function(space, key, opts)
                return self:_select(space.id, 0, key, opts)
            end,

            delete = function(space, key)
                return self:_delete(space.id, key)
            end,

            update = function(space, key, oplist)
                return self:_update(space.id, key, oplist)
            end,

            get = function(space, key)
                local res = self:_select(space.id, 0, key,
                                    { limit = 2, iterator = 'EQ' })
                if #res == 0 then
                    return
                end
                if #res == 1 then
                    return res[1]
                end
                box.raise(box.error.MORE_THAN_ONE_TUPLE,
                    "More than one tuple found by get()")
            end
        }
    }
end

local function index_metatable(self)
    return {
        __index = {
            select = function(idx, key, opts)
                return self:_select(idx.space.id, idx.id, key, opts)
            end,



            get = function(idx, key)
                local res = self:_select(idx.space.id, idx.id, key,
                                    { limit = 2, iterator = 'EQ' })
                if #res == 0 then
                    return
                end
                if #res == 1 then
                    return res[1]
                end
                box.raise(box.error.MORE_THAN_ONE_TUPLE,
                    "More than one tuple found by get()")
            end,

            min = function(idx, key)
                local res = self:_select(idx.space.id, idx.id, key,
                    { limit = 1, iterator = 'GE' })
                if #res > 0 then
                    return res[1]
                end
            end,

            max = function(idx, key)
                local res = self:_select(idx.space.id, idx.id, key,
                    { limit = 1, iterator = 'LE' })
                if #res > 0 then
                    return res[1]
                end
            end,

            count = function(idx, key)
                local proc = string.format('box.space.%s.index.%s:count',
                    idx.space.name, idx.name)
                local res = self:call(proc, key)
                if #res > 0 then
                    return res[1][0]
                end
            end
        }
    }
end


local remote = {}

local remote_methods = {
    proto = proto,

    new = function(cls, host, port, opts)
        local self = {}
        setmetatable(self, getmetatable(cls))

        self.is_instance = true
        self.host = host
        self.port = port
        self.opts = opts
        if self.opts == nil then
            self.opts = {}
        end

        if self.host == nil then
            self.host = 'localhost'
        end

        self.is_run = true
        self.state = 'init'

        self.ch = { sync = {}, fid = {} }
        self.wait = { state = {} }
        self.timeouts = {}


        fiber.wrap(function() self:_connect_worker() end)
        fiber.wrap(function() self:_read_worker() end)
        fiber.wrap(function() self:_write_worker() end)

        if self.opts.wait_connected == nil or self.opts.wait_connected then
            self:wait_connected()
        end

        return self
    end,


    ping    = function(self)
        if not self:is_connected() then
            return false
        end
        local sync = self.proto:sync()
        local req = self.proto.ping(sync)

        local res = self:_request('ping', false)


        if res == nil then
            return false
        end

        if res.hdr[CODE] == 0 then
            return true
        end
        return false
    end,

    call    = function(self, proc, ...)
        local res = self:_request('call', true, proc, {...})
        return res.body[DATA]
    end,

    is_connected = function(self)
        if self.state == 'active' then
            return true
        end
        if self.state == 'activew' then
            return true
        end
        return false
    end,

    wait_connected = function(self, timeout)
        return self:_wait_state({ 'active', 'activew', 'closed' }, timeout)
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
                    box.raise(box.error.TIMEOUT, 'Timeout exceeded')
                end
                return cn
            end
        }
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

        log.warn(emsg)
        self.error = emsg
        self.space = {}
        self:_switch_state('error')
        self:_error_waiters(emsg)
        self.rbuf = ''
        self.wbuf = ''
        self.handshake = ''
    end,

    _error_waiters = function(self, emsg)
        local waiters = self.ch.sync
        self.ch.sync = {}
        for sync, channel in pairs(waiters) do
            channel:put{
                hdr = {
                    [TYPE] = ERROR,
                    [CODE] = box.error.NO_CONNECTION,
                    [SYNC] = sync
                },
                body = {
                    [ERROR] = emsg
                }
            }
        end
    end,

    _check_response = function(self)
        while true do
            if #self.rbuf < 5 then
                break
            end

            local len, off = msgpack.decode(self.rbuf)

            if len < #self.rbuf - off then
                return
            end

            local hdr, body
            hdr, off = msgpack.decode(self.rbuf, off)
            if off < #self.rbuf then
                body, off = msgpack.decode(self.rbuf, off)
            else
                body = {}
            end
            self.rbuf = string.sub(self.rbuf, off + 1)

            local sync = hdr[SYNC]

            if self.ch.sync[sync] ~= nil then
                self.ch.sync[sync]:put({ hdr = hdr, body = body })
                self.ch.sync[sync] = nil
            else
                log.warn("Unexpected response %s", sync)
            end
        end
    end,

    _switch_state = function(self, state)
        if self.state == state then
            return
        end
        self.state = state

        local list = self.wait.state[ state ]
        self.wait.state[ state ] = nil

        if list == nil then
            return
        end

        for _, fid in pairs(list) do
            if self.ch.fid[fid] ~= nil then
                self.ch.fid[fid]:put(true)
                self.ch.fid[fid] = nil
            end
        end
    end,

    _wait_state = function(self, states, timeout)
        if timeout == nil then
            timeout = TIMEOUT_INFINITY
        end
        while timeout > 0 do
            local started = fiber.time()
            for _, state in pairs(states) do
                if self.state == state then
                    return true
                end
            end

            local fid = fiber.id()
            local ch = fiber.channel()
            for _, state in pairs(states) do
                if self.wait.state[state] == nil then
                    self.wait.state[state] = {}
                end
                self.wait.state[state][fid] = fid
            end

            self.ch.fid[fid] = ch
            local res
            res = ch:get(timeout)
            self.ch.fid[fid] = nil

            local has_state = false
            for _, state in pairs(states) do
                if self.wait.state[state] ~= nil then
                    self.wait.state[state][fid] = nil
                end
                if self.state == state then
                    has_state = true
                end
            end

            if has_state or res == nil then
                return res
            end

            timeout = timeout - (fiber.time() - started)
        end
    end,

    _connect_worker = function(self)
        fiber.name('net.box.connector')
        while true do
            self:_wait_state{ 'init', 'error', 'closed' }
            if self.state == 'closed' then
                return
            end

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
                self.handshake = self.s:read(128)
                if self.handshake == nil then
                    self:_fatal(errno.strerror(errno()))
                elseif string.len(self.handshake) ~= 128 then
                    self:_fatal("Can't read handshake")
                else

                    self.wbuf = ''
                    self.rbuf = ''

                    local s, e = pcall(function()
                        self:_auth()
                    end)
                    if not s then
                        self:_fatal(e)
                    end
                        
                    xpcall(function() self:_load_schema() end,
                        function(e)
                            log.info("Can't load schema: %s", tostring(e))
                        end)
                   
                    if self.state ~= 'error' and self.state ~= 'closed' then
                        self:_switch_state('active')
                    end
                end
            end
        end
    end,

    _auth = function(self)
        if self.opts.user == nil or self.opts.password == nil then
            self:_switch_state 'authen'
            return
        end

        self:_switch_state 'auth'

        local auth_res = self:_request_internal('auth',
            false, self.opts.user, self.opts.password, self.handshake)

        if auth_res.hdr[CODE] ~= 0 then
            self:_fatal(auth_res.body[ERROR])
            return
        end

        self:_switch_state 'authen'
    end,

    -- states wakeup _read_worker
    _r_states = {
        'active', 'activew', 'schema', 'schemaw', 'closed', 'auth', 'authw'
    },
    -- states wakeup _write_worker
    _rw_states = { 'activew', 'schemaw', 'closed', 'authw' },

    _is_r_state = function(self)
        for _, state in pairs(self._r_states) do
            if state == self.state then
                return true
            end
        end
        return false
    end,

    _is_rw_state = function(self)
        for _, state in pairs(self._rw_states) do
            if state == self.state then
                return true
            end
        end
        return false
    end,


    _load_schema = function(self)
        if self.state ~= 'authen' then
            self:_fatal 'Can not load schema from the state'
            return
        end
        
        self:_switch_state('schema')

        local spaces = self:_request_internal('select',
            true, box.schema.SPACE_ID, 0, nil, { iterator = 'ALL' }).body[DATA]
        local indexes = self:_request_internal('select',
            true, box.schema.INDEX_ID, 0, nil, { iterator = 'ALL' }).body[DATA]

        local sl = {}


        for _, space in pairs(spaces) do
            local name = space[2]
            local id = space[0]
            local engine = space[3]
            local field_count = space[4]

            local s = {
                id              = id,
                name            = name,
                engine          = engine,
                field_count     = field_count,
                enabled         = true,
                index           = {}

            }
            if #space > 5 and string.match(space[5], 'temporary') then
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
                space   = index[0],
                id      = index[1],
                name    = index[2],
                type    = string.upper(index[3]),
                parts   = {},
            }

            if index[4] == 0 then
                idx.unique = false
            else
                idx.unique = true
            end

            for k = 0, index[5] - 1 do
                local pktype = index[6 + k * 2 + 1]
                local pkfield = index[6 + k * 2]

                local pk = {
                    type = string.upper(pktype),
                    fieldno = pkfield
                }
                idx.parts[k] = pk
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
        while self.state ~= 'closed' do
            self:_wait_state(self._r_states)
            if self.state == 'closed' then
                break
            end

            if self.s:readable(.5) then
                if self.state == 'closed' then
                    break
                end

                if self:_is_r_state() then
                    local data = self.s:sysread(4096)

                    if data ~= nil then
                        self.rbuf = self.rbuf .. data
                        self:_check_response()
                    else
                        self:_fatal(errno.strerror(errno()))
                    end
                end
            end
        end
    end,

    _to_wstate = { active = 'activew', schema = 'schemaw', auth = 'authw' },
    _to_rstate = { activew = 'active', schemaw = 'schema', authw = 'auth' },

    _write_worker = function(self)
        fiber.name('net.box.write')
        while self.state ~= 'closed' do
            self:_wait_state(self._rw_states)
            
            if self.state == 'closed' then
                break
            end

            if string.len(self.wbuf) == 0 then

                local wstate = self._to_rstate[self.state]
                if wstate ~= nil then
                    self:_switch_state(wstate)
                end

            elseif self.s:writable(.5) then

                if self.state == 'closed' then
                    break
                end
                if self:_is_rw_state() then
                    if #self.wbuf > 0 then
                        local written = self.s:syswrite(self.wbuf)
                        if written ~= nil then
                            self.wbuf = string.sub(self.wbuf,
                                tonumber(1 + written))
                        else
                            self:_fatal(errno.strerror(errno()))
                        end
                    end
                end

            end
        end
    end,


    _request = function(self, name, raise, ...)

        local fid = fiber.id()
        if self.timeouts[fid] == nil then
            self.timeouts[fid] = TIMEOUT_INFINITY
        end

        local started = fiber.time()

        self:_wait_state({ 'active', 'activew', 'closed' }, self.timeouts[fid])

        self.timeouts[fid] = self.timeouts[fid] - (fiber.time() - started)

        if self.state == 'closed' then
            if raise then
                box.raise(box.error.NO_CONNECTION,
                    "Connection was closed")
            end
        end

        if self.timeouts[fid] <= 0 then
            self.timeouts[fid] = nil
            if raise then
                box.raise(box.error.TIMEOUT, 'Timeout exceeded')
            else
                return {
                    hdr = { [CODE] = box.error.TIMEOUT },
                    body = { [ERROR] = 'Timeout exceeded' }
                }
            end
        end

        return self:_request_internal(name, raise, ...)
    end,

    _request_internal = function(self, name, raise, ...)
        
        local fid = fiber.id()
        if self.timeouts[fid] == nil then
            self.timeouts[fid] = TIMEOUT_INFINITY
        end

        local sync = self.proto:sync()
        local request = self.proto[name](sync, ...)

        self.wbuf = self.wbuf .. request

        local wstate = self._to_wstate[self.state]
        if wstate ~= nil then
            self:_switch_state(wstate)
        end

        local ch = fiber.channel()

        self.ch.sync[sync] = ch

        local response = ch:get(self.timeouts[fid])
        self.ch.sync[sync] = nil
        self.timeouts[fid] = nil


        if response == nil then
            if raise then
                box.raise(box.error.TIMEOUT, 'Timeout exceeded')
            else
                return {
                    hdr = { [CODE] = box.error.TIMEOUT },
                    body = { [ERROR] = 'Timeout exceeded' }
                }
            end
        end

        if raise and response.hdr[CODE] ~= 0 then
            box.raise(response.hdr[CODE], response.body[ERROR])
        end

        if response.body[DATA] ~= nil then
            for i, v in pairs(response.body[DATA]) do
                response.body[DATA][i] = box.tuple.new(response.body[DATA][i])
            end
        end

        return response
    end,

    -- private (low level) methods
    _select = function(self, spaceno, indexno, key, opts)
        local res = self:_request('select', true, spaceno, indexno, key, opts)
        return res.body[DATA]
    end,

    _insert = function(self, spaceno, tuple)
        local res = self:_request('insert', true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    _replace = function(self, spaceno, tuple)
        local res = self:_request('replace', true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    _delete  = function(self, spaceno, key)
        local res = self:_request('delete', true, spaceno, key)
        return one_tuple(res.body[DATA])
    end,

    _update = function(self, spaceno, key, oplist)
        local res = self:_request('update', true, spaceno, key, oplist)
        return one_tuple(res.body[DATA])
    end
}

setmetatable(remote, { __index = remote_methods })

box.ping = function() return true end
box.close = function() return true end
box.wait_connected = function() return true end
box.call = function(_box, proc_name, ...)
    local proc = { package.loaded['box.internal'].call_loadproc(proc_name) }
    local result
    if #proc == 2 then
        result = { proc[1](proc[2], ...) }
    else
        result = { proc[1](...) }
    end

    if #result == 1 and type(result[1]) == 'table' then
        result = result[1]
    end

    for i, v in pairs(result) do
        result[i] = box.tuple.new(v)
    end
    return result
end

box.timeout = function() return box end

return remote


