-- box_net.lua (internal file)

box.net = {

--
-- The idea of box.net.box implementation is that
-- most calls are simply wrappers around 'process'
-- function. The embedded 'process' function sends
-- requests to the local server, the remote 'process'
-- routes requests to a remote.
--
    box = {
        PING = 0,
        SELECT = 1,
        INSERT = 2,
        REPLACE = 3,
        UPDATE = 4,
        DELETE = 5,
        CALL = 6,

        CODE = 0x00,
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

        delete = function(self, space, ...)
            local key_part_count = select('#', ...)
            return self:process(box.net.box.DELETE,
                    box.pack('iV', space,
                        key_part_count, ...))
        end,

        replace = function(self, space, ...)
            local field_count = select('#', ...)
            return self:process(box.net.box.REPLACE,
                    box.pack('iV', space, field_count, ...))
        end,

        -- insert a tuple (produces an error if the tuple already exists)
        insert = function(self, space, ...)
            local field_count = select('#', ...)
            return self:process(box.net.box.INSERT,
                   box.pack('iV', space, field_count, ...))
        end,

        -- update a tuple
        update = function(self, space, key, ops)
            return self:process(box.net.box.UPDATE,
                box.pack('iVa', space, 1, key, msgpack.encode(ops)))
        end,

        select_limit = function(self, space, index, offset, limit, ...)
            local key_part_count = select('#', ...)
            return self:process(box.net.box.SELECT,
                   box.pack('iiiiV',
                         space,
                         index,
                         offset,
                         limit,
                         key_part_count, ...))
        end,

        select = function(self, space, key)
            return self:process(box.net.box.SELECT,
                    box.pack('iiiiV',
                         space,
                         0,
                         0, -- offset
                         4294967295, -- limit
                         1, key))
        end,


        ping = function(self)
            return self:process(box.net.box.PING, '')
        end,

        call    = function(self, proc_name, tuple)
            assert(type(proc_name) == 'string')
            return self:process(box.net.box.CALL,
                box.pack('pV', proc_name, 1, tuple))
        end,

        select_range = function(self, sno, ino, limit, ...)
            return self:call(
                'box.select_range',
                sno,
                ino,
                limit,
                ...
            )
        end,

        select_reverse_range = function(self, sno, ino, limit, ...)
            return self:call(
                'box.select_reverse_range',
                sno,
                ino,
                limit,
                ...
            )
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

                    error(string.format('Can not find "box.net.box.%s" function',
                            name))
                end
            });

            return wrapper
        end,
    },


    -- local tarantool
    self = {
        process = function(self, ...)
            return box.process(...)
        end,

        select_range = function(self, sno, ino, limit, ...)
            return box.space[sno].index[ino]:select_range(limit, ...)
        end,

        select_reverse_range = function(self, sno, ino, limit, ...)
            return box.space[sno].index[ino]
                :select_reverse_range(limit, ...)
        end,

        -- for compatibility with the networked version,
        -- implement call
        call = function(self, proc_name, ...)
            local proc = { box.call_loadproc(proc_name) }
            if #proc == 2 then
                return proc[1](proc[2], ...)
            else
                return proc[1](...)
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
        end
    }
}

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
            wch = box.ipc.channel(1),

            -- ready socket channel
            rch = box.ipc.channel(1),
        },



        process = function(self, op, request)
            local started = box.time()
            local timeout = self.request_timeout
            self.request_timeout = nil

            -- get an auto-incremented request id
            local sync = self.processing:next_sync()
            self.processing[sync] = box.ipc.channel(1)
            local header = msgpack.encode({[box.net.box.CODE] = op, [box.net.box.SYNC] = sync})
            request = msgpack.encode(header:len() + request:len())..
                      header..request

            if timeout ~= nil then
                timeout = timeout
                if not self.processing.wch:put(request, timeout) then
                    self.processing[sync] = nil
                    return nil
                end

                timeout = timeout - (box.time() - started)
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
                    return unpack(totuples(body[box.net.box.DATA]))
                end
            else
                error(res[2])
            end
        end,


        try_connect = function(self)
            if self.s ~= nil then
                return true
            end

            local sc = box.socket.tcp()
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

            self.s = sc

            return true
        end,

        read_response = function(self)
            if self.s == nil then
                return
            end
            local blen = self.s:recv(5)
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
                    box.fiber.sleep(self.reconnect_timeout)
                end

                -- wakeup write fiber
                self.processing.rch:put(true, 0)

                while not self.closed do
                    local resp = self:read_response()
                    header, offset = msgpack.next(resp);
                    code = header[box.net.box.CODE]
                    sync = header[box.net.box.SYNC]
                    if sync == nil then
                        break
                    end

                    if self.processing[sync] ~= nil then
                        self.processing[sync]:put({true, code, resp:sub(offset)}, 0)
                    else
                        print("Unexpected response ", sync)
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
            message = string.format(message, ...)
            self.s = nil
            for sync, ch in pairs(self.processing) do
                if type(sync) == 'number' then
                    ch:put({ false, message }, 0)
                end
            end
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

    remote.irfiber = box.fiber.wrap(remote.rfiber, remote)
    remote.iwfiber = box.fiber.wrap(remote.wfiber, remote)

    return remote
end

-- vim: set et ts=4 sts
