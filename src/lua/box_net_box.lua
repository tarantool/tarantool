-- net_box.lua (internal file)

local msgpack = require 'msgpack'
local fiber = require 'fiber'
local socket = require 'socket'
local log = require 'log'
local errno = require 'errno'
local yaml = require 'yaml'
local json = require 'json'

local CODE              = 0
local PING              = 64
local SELECT            = 1
local INSERT            = 2
local REPLACE           = 3
local UPDATE            = 4
local DELETE            = 5
local CALL              = 6
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
local DATA              = 0x30
local ERROR             = 0x31
local GREETING_SIZE     = 128

local TIMEOUT_INFINITY  = 500 * 365 * 86400


local function request(header, body)

    header = msgpack.encode(header)
    body = msgpack.encode(body)

    local len = msgpack.encode(string.len(header) + string.len(body))


    return len .. header .. body
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


}


local function space_metatable(self)
    return {
        __index = {
            insert  = function(space, tuple)
                return self:insert(space.id, tuple)
            end,

            replace = function(space, tuple)
                return self:replace(space.id, tuple)
            end,

            select = function(space, key, opts)
                return self:select(space.id, 0, key, opts)
            end,

            delete = function(space, key)
                return self:delete(space.id, key)
            end,

            update = function(space, key, oplist)
                return self:update(space.id, key, oplist)
            end
        }
    }
end

local function index_metatable(self)
    return {
        __index = {
            select = function(idx, key, opts)
                return self:select(idx.space.id, idx.id, key, opts)
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


        fiber.wrap(function() self:connect_worker() end)
        fiber.wrap(function() self:read_worker() end)
        fiber.wrap(function() self:write_worker() end)

        return self
    end,

    check_response = function(self)
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

    is_connected = function(self)
        if self.state == 'active' then
            return true
        end
        if self.state == 'activew' then
            return true
        end
        return false
    end,

    can_request = function(self)
        if self:is_connected() then
            return true
        end
        if self.state == 'schema' or self.state == 'schemaw' then
            return true
        end
        return false
    end,

    switch_state = function(self, state)
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

    wait_state = function(self, states, timeout)
        while true do
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
            if timeout ~= nil then
                res = ch:get(timeout)
            else
                res = ch:get()
            end
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
        end
    end,

    connect_worker = function(self)
        fiber.name('net.box.connector')
        while true do
            self:wait_state{ 'init', 'error', 'closed' }
            if self.state == 'closed' then
                return
            end

            if self.state == 'error' then
                if self.opts.reconnect_after == nil then
                    self:switch_state('closed')
                    return
                end
                fiber.sleep(self.opts.reconnect_after)
            end

            self:switch_state('connecting')

            self.s = socket.tcp_connect(self.host, self.port)
            if self.s == nil then
                self:fatal(errno.strerror(errno()))
                return
            end

            -- on_connect
            self:switch_state('handshake')
            self.handshake = self.s:read(128)
            if self.handshake == nil then
                self:fatal(errno.strerror(errno()))
                return
            end
            if string.len(self.handshake) ~= 128 then
                self:fatal("Can't read handshake")
                return
            end

            self.wbuf = ''
            self.rbuf = ''

            -- if user didn't point auth parameters, skip 'auth' state
            if self.opts.user ~= nil and self.opts.password ~= nil then
                error("Auth request is not implemented yet")
            end

            local s, e = pcall(function() self:load_schema() end)
            if not s then
                self:fatal(e)
            else
                self:switch_state('active')
            end

        end
    end,

    fatal = function(self, efmt, ...)
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
        self:switch_state('error')


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
                    [ERROR] = self.error
                }
            }
        end
        self.rbuf = ''
        self.wbuf = ''
    end,

    load_schema = function(self)
        self:switch_state('schema')

        local spaces = self:request_internal('select',
            true, box.schema.SPACE_ID, 0, nil, { iterator = 'ALL' }).body[DATA]

        local indexes = self:request_internal('select',
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

    read_worker = function(self)
        fiber.name('net.box.read')
        while true do
            self:wait_state{'active', 'activew', 'closed', 'schema', 'schemaw' }
            if self.state == 'closed' then
                return
            end

            if self.s:readable(.5) and self:can_request() then
                local data = self.s:sysread(4096)

                if data ~= nil then
                    self.rbuf = self.rbuf .. data
                    self:check_response()
                else
                    self:fatal(errno.strerror(errno()))
                end
            end
        end
    end,

    write_worker = function(self)
        fiber.name('net.box.write')
        while true do
            self:wait_state{'activew', 'closed', 'schemaw'}

            if self.state == 'closed' then
                return
            end

            if #self.wbuf == 0 then

                if self.state == 'activew' then
                    self:switch_state('active')
                elseif self.state == 'schemaw' then
                    self:switch_state('schema')
                end

            end

            if self.s:writable(.5) then

                if self.state == 'activew' or self.state == 'schemaw' then
                    if #self.wbuf > 0 then
                        local written = self.s:syswrite(self.wbuf)
                        if written ~= nil then
                            self.wbuf = string.sub(self.wbuf,
                                tonumber(1 + written))
                        else
                            self:fatal(errno.strerror(errno()))
                        end
                    end
                end
            end
        end
    end,



    -- iproto methods
    request = function(self, name, raise, ...)

        self:wait_state{ 'active', 'activew', 'closed' }

        if self.state == 'closed' then
            box.raise(box.error.NO_CONNECTION,
                "Connection was closed")
        end

        return self:request_internal(name, raise, ...)
    end,

    request_internal = function(self, name, raise, ...)

        local sync = self.proto:sync()
        local request = self.proto[name](sync, ...)

        self.wbuf = self.wbuf .. request

        if self.state == 'active' then
            self:switch_state('activew')
        elseif self.state == 'schema' then
            self:switch_state('schemaw')
        end

        local ch = fiber.channel()

        self.ch.sync[sync] = ch

        local response = ch:get()
        self.ch.sync[sync] = nil

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

    ping    = function(self)
        if not self:is_connected() then
            return false
        end
        local sync = self.proto:sync()
        local req = self.proto.ping(sync)

        local res = self:request('ping', false)


        if res == nil then
            return false
        end

        if res.hdr[CODE] == 0 then
            return true
        end
        return false
    end,

    call    = function(self, proc, ...)
        local res = self:request('call', true, proc, {...})
        return res.body[DATA]
    end,

    select = function(self, spaceno, indexno, key, opts)
        local res = self:request('select', true, spaceno, indexno, key, opts)
        return res.body[DATA]
    end,

    insert = function(self, spaceno, tuple)
        local res = self:request('insert', true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    replace = function(self, spaceno, tuple)
        local res = self:request('replace', true, spaceno, tuple)
        return one_tuple(res.body[DATA])
    end,

    delete  = function(self, spaceno, key)
        local res = self:request('delete', true, spaceno, key)
        return one_tuple(res.body[DATA])
    end,

    update = function(self, spaceno, key, oplist)
        local res = self:request('update', true, spaceno, key, oplist)
        return one_tuple(res.body[DATA])
    end
}

setmetatable(remote, { __index = remote_methods })

box.ping = function() return true end
box.close = function() return true end

return remote


