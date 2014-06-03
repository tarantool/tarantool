-- box_net.lua (internal file)

(function()

local msgpack = require('msgpack')
local fiber = require('fiber')
local socket = require('socket')
local internal = require('box.internal')

local function keify(key)
    if key == nil then
        return {}
    end
    if type(key) == "table" then
        return key
    end
    return {key}
end


local function sprintf(fmt, ...) return string.format(fmt, ...) end
local function printf(fmt, ...) return print(sprintf(fmt, ...)) end
local function errorf(fmt, ...) error(sprintf(fmt, ...)) end




local function rpc_call(r, slf, ...)

    local path = rawget(r, 'path')
    local args = { slf, ... }
    if type(slf) == 'table' then
        if rawget(slf, 'path') ~= nil and rawget(slf, 'r') ~= nil then
            path = slf.path .. ':' .. r.method
            args = { ... }
        end
    end

    
    return r.r:call(path, unpack(args))
end

local function rpc_index(r, name)
    local o = { r = rawget(r, 'r') }
    local path = rawget(r, 'path')
    if path == nil then
        path = name
    else
        if type(name) == 'string' then
            path = sprintf("%s.%s", path, name)
        elseif type(name) == 'number' then
            path = sprintf("%s[%s]", path, name)
        else
            errorf("Wrong path subitem: %s", tostring(name))
        end
    end
    o.path = path
    o.method = name

    setmetatable(o, {
        __index = rpc_index,
        __call  = rpc_call,
    })

    rawset(r, name, o)
    return o
end



box.net = {

--
-- The idea of box.net.box implementation is that
-- most calls are simply wrappers around 'process'
-- function. The embedded 'process' function sends
-- requests to the local server, the remote 'process'
-- routes requests to a remote.
--
    box = {
        PING = 64,
        SELECT = 1,
        INSERT = 2,
        REPLACE = 3,
        UPDATE = 4,
        DELETE = 5,
        CALL = 6,

        TYPE = 0x00,
        SYNC = 0x01,
        SPACE_ID = 0x10,
        INDEX_ID = 0x11,
        LIMIT = 0x12,
        OFFSET = 0x13,
        ITERATOR = 0x14,
        KEY = 0x20,
        TUPLE = 0x21,
        FUNCTION_NAME = 0x22,
        DATA = 0x30,
        ERROR = 0x31,
        GREETING_SIZE = 128,

        delete = function(self, space, key)
            local t = self:process(box.net.box.DELETE,
                    msgpack.encode({
                        [box.net.box.SPACE_ID] = space,
                        [box.net.box.KEY] = keify(key)
                        }))
            if t[1] ~= nil then
                return t[1]
            else
                return
            end
        end,

        replace = function(self, space, tuple)
            local t = self:process(box.net.box.REPLACE,
                    msgpack.encode({
                        [box.net.box.SPACE_ID] = space,
                        [box.net.box.TUPLE] = tuple
                        }))
            if t[1] ~= nil then
                return t[1]
            else
                return
            end
        end,

        -- insert a tuple (produces an error if the tuple already exists)
        insert = function(self, space, tuple)
            local t = self:process(box.net.box.INSERT,
                    msgpack.encode({
                        [box.net.box.SPACE_ID] = space,
                        [box.net.box.TUPLE] = tuple
                        }))
            if t[1] ~= nil then
                return t[1]
            else
                return
            end
        end,

        -- update a tuple
        update = function(self, space, key, ops)
            local t = self:process(box.net.box.UPDATE,
                    msgpack.encode({
                        [box.net.box.SPACE_ID] = space,
                        [box.net.box.KEY] = keify(key),
                        [box.net.box.TUPLE] = ops
                        }))
            if t[1] ~= nil then
                return t[1]
            else
                return
            end
        end,

        get = function(self, space, key)
            key = keify(key)
            local result = self:process(box.net.box.SELECT,
                msgpack.encode({
                    [box.net.box.SPACE_ID] = space,
                    [box.net.box.KEY] = key,
                    [box.net.box.ITERATOR] = box.index.EQ,
                    [box.net.box.OFFSET] = 0,
                    [box.net.box.LIMIT] = 2
                }))
            if #result == 0 then
                return
            elseif #result == 1 then
                return result[1]
            else
                box.raise(box.error.ER_MORE_THAN_ONE_TUPLE,
                    "More than one tuple found without 'limit'")
            end
        end,

        select = function(self, space, key, opts)
            local offset = 0
            local limit = 4294967295
            local iterator = box.index.EQ

            key = keify(key)
            if #key == 0 then
                iterator = box.index.ALL
            end

            if opts ~= nil then
                if opts.offset ~= nil then
                    offset = tonumber(opts.offset)
                end
                if type(opts.iterator) == "string" then
                    opts.iterator = box.index[opts.iterator]
                end
                if opts.iterator ~= nil then
                    iterator = tonumber(opts.iterator)
                end
                if opts.limit ~= nil then
                    limit = tonumber(opts.limit)
                end
            end
            local result = self:process(box.net.box.SELECT,
                msgpack.encode({
                    [box.net.box.SPACE_ID] = space,
                    [box.net.box.KEY] = key,
                    [box.net.box.ITERATOR] = iterator,
                    [box.net.box.OFFSET] = offset,
                    [box.net.box.LIMIT] = limit
                }))
            return result
        end,

        ping = function(self)
            return self:process(box.net.box.PING, '')
        end,

        call    = function(self, name, ...)
            assert(type(name) == 'string')
            return self:process(box.net.box.CALL,
                msgpack.encode({
                    [box.net.box.FUNCTION_NAME] = name,
                    [box.net.box.TUPLE] = {...}}))
        end,

        -- To make use of timeouts safe across multiple
        -- concurrent fibers do not store timeouts as
        -- part of conection state, but put it inside
        -- a helper object.

        timeout = function(self, timeout)

            local wrapper = {}

            setmetatable(wrapper, {
                __index = function(wrp, name, ...)
                    local func = self[name]
                    if func ~= nil then
                        return
                            function(wr, ...)
                                self.request_timeout = timeout
                                return func(self, ...)
                            end
                    end

                    errorf('Can not find "box.net.box.%s" function', name)
                end
            });

            return wrapper
        end,
    },


    -- local tarantool
    self = {
        process = function(self, ...)
            return internal.process(...)
        end,

        -- for compatibility with the networked version,
        -- implement call
        call = function(self, proc_name, ...) 
            local proc = { internal.call_loadproc(proc_name) }
            if #proc == 2 then
                return { proc[1](proc[2], ...) }
            else
                return { proc[1](...) }
            end
        end,

        ping = function(self)
            return true
        end,

        -- local tarantool doesn't provide timeouts
        timeout = function(self, timeout)
            return self
        end,

        close = function(self)
            return true
        end,

    }
}

box.net.box.put = box.net.box.replace; -- put is an alias for replace

-- box.net.self rpc works like remote.rpc
box.net.self.rpc = { r = box.net.self }
setmetatable(box.net.self.rpc, { __index = rpc_index })


--
-- Make sure box.net.box.select(conn, ...) works
-- just as well as conn:select(...)
--
setmetatable(box.net.self, { __index = box.net.box })

box.net.box.new = function(host, port, reconnect_timeout)
    if reconnect_timeout == nil then
        reconnect_timeout = 0
    else
        reconnect_timeout = reconnect_timeout
    end

    local remote = {
        host                = host,
        port                = port,
        reconnect_timeout   = reconnect_timeout,
        closed              = false,
        timedout            = {},

        title   = function(self)
            return sprintf('%s:%s', tostring(self.host), tostring(self.port))
        end,

        processing = {
            last_sync = 0,
            next_sync = function(self)
                while true do
                    self.last_sync = self.last_sync + 1
                    if self[ self.last_sync ] == nil then
                        return self.last_sync
                    end

                    if self.last_sync > 0x7FFFFFFF then
                        self.last_sync = 0
                    end
                end
            end,

            -- write channel
            wch = fiber.channel(1),

            -- ready socket channel
            rch = fiber.channel(1),
        },



        process = function(self, op, request)
            local started = fiber.time()
            local timeout = self.request_timeout
            self.request_timeout = nil

            -- get an auto-incremented request id
            local sync = self.processing:next_sync()
            self.processing[sync] = fiber.channel(1)
            local header = msgpack.encode{
                    [box.net.box.TYPE] = op, [box.net.box.SYNC] = sync
            }
            request = msgpack.encode(header:len() + request:len())..
                      header..request

            if timeout ~= nil then
                timeout = timeout
                if not self.processing.wch:put(request, timeout) then
                    self.processing[sync] = nil
                    return nil
                end

                timeout = timeout - (fiber.time() - started)
            else
                self.processing.wch:put(request)
            end

            local res
            if timeout ~= nil then
                res = self.processing[sync]:get(timeout)
            else
                res = self.processing[sync]:get()
            end
            self.processing[sync] = nil

            -- timeout
            if res == nil then
                self.timedout[ sync ] = true
                if op == box.net.box.PING then
                    return false
                else
                    return nil
                end
            end

            local function totuples(t)
                res = {}
                for k, v in pairs(t) do
                    table.insert(res, box.tuple.new(v))
                end
                return res
            end

            -- results { status, response } received
            if res[1] then
                if op == box.net.box.PING then
                    return true
                else
                    local code = res[2]
                    local body = msgpack.decode(res[3])
                    if code ~= 0 then
                        box.raise(code, body[box.net.box.ERROR])
                    end
                    return totuples(body[box.net.box.DATA])
                end
            else
                if op == 65280 then
                    return false
                end
                errorf('%s: %s', self:title(), res[2])
            end
        end,


        try_connect = function(self)
            if self.s ~= nil then
                return true
            end

            local sc = socket.tcp()
            if sc == nil then
                self:fatal("Can't create socket")
                return false
            end

            local s = { sc:connect( self.host, self.port ) }
            if s[1] == nil then
                self:fatal("Can't connect to %s:%s: %s",
                    self.host, self.port, s[4])
                return false
            end
            sc:recv(box.net.box.GREETING_SIZE)

            self.s = sc

            return true
        end,

        read_response = function(self)
            if self.s == nil then
                return
            end
            local blen = self.s:recv(5)
            if string.len(blen) < 5 then
                self:fatal("The server has closed connection")
                return
            end
            blen = msgpack.decode(blen)

            local body = ''
            if blen > 0 then
                res = { self.s:recv(blen) }
                if res[4] ~= nil then
                    self:fatal("Error while reading socket: %s", res[4])
                    return
                end
                body = res[1]
                if string.len(body) ~= blen then
                    self:fatal("Unexpected eof while reading body")
                    return
                end
            end
            return body
        end,

        rfiber = function(self)
            while not self.closed do
                while not self.closed do
                    if self:try_connect(self.host, self.port) then
                        break
                    end
                    -- timeout between reconnect attempts
                    fiber.sleep(self.reconnect_timeout)
                end

                -- wakeup write fiber
                self.processing.rch:put(true, 0)

                while not self.closed do
                    local resp = self:read_response()
                    if resp == nil or string.len(resp) == 0 then
                        break
                    end
                    local header, offset = msgpack.decode(resp);
                    local code = header[box.net.box.TYPE]
                    local sync = header[box.net.box.SYNC]
                    if sync == nil then
                        break
                    end

                    if self.processing[sync] ~= nil then
                        self.processing[sync]:put({true, code, resp:sub(offset)}, 0)
                    else
                        if self.timedout[ sync ] then
                            self.timedout[ sync ] = nil
                            printf("Timed out response from %s", self:title())
                        else
                            printf("Unexpected response %s from %s",
                                sync, self:title())
                        end
                    end
                end

            end
            self.irfiber = nil
        end,


        wfiber = function(self)
            local request
            while not self.closed do
                while self.s == nil do
                    self.processing.rch:get(1)
                end
                if request == nil then
                    request = self.processing.wch:get(1)
                end
                if self.s ~= nil and request ~= nil then
                    local res = { self.s:send(request) }
                    if res[1] ~= string.len(request) then
                        self:fatal("Error while write socket: %s", res[4])
                    end
                    request = nil
                end
            end
            self.iwfiber = nil
        end,

        fatal = function(self, message, ...)
            message = sprintf(message, ...)
            self.s = nil
            for sync, ch in pairs(self.processing) do
                if type(sync) == 'number' then
                    ch:put({ false, message }, 0)
                end
            end
            self.timedout = {}
        end,

        close = function(self)
            if self.closed then
                error("box.net.box: already closed")
            end
            self.closed = true
            local message = 'box.net.box: connection was closed'
            self.process = function()
                error(message)
            end
            self:fatal(message)

            -- wake up write fiber
            self.processing.rch:put(true, 0)
            self.processing.wch:put(true, 0)
            return true
        end
    }


    setmetatable( remote, { __index = box.net.box } )

    remote.irfiber = fiber.wrap(remote.rfiber, remote)
    remote.iwfiber = fiber.wrap(remote.wfiber, remote)

    remote.rpc = { r = remote }
    setmetatable(remote.rpc, { __index = rpc_index })
    
    return remote

end

end)()
-- vim: set et ts=4 sts
