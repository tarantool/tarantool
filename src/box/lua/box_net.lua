-- box_net.lua (internal file)

box.net = {

    PING = 0,
    SELECT = 1,
    INSERT = 2,
    REPLACE = 3,
    UPDATE = 4,
    DELETE = 5,
    CALL = 6,

--
-- The idea of box.net.box implementation is that
-- most calls are simply wrappers around 'process'
-- function. The embedded 'process' function sends
-- requests to the local server, the remote 'process'
-- routes requests to a remote.
--
    box = {
        delete = function(self, space, ...)
            local key_part_count = select('#', ...)
            return self:process(box.net.DELETE,
                    box.pack('iiV',
                        space,
                        box.flags.BOX_RETURN_TUPLE,  -- flags
                        key_part_count, ...))
        end,

        replace = function(self, space, ...)
            local field_count = select('#', ...)
            return self:process(box.net.REPLACE,
                    box.pack('iiV',
                        space,
                        box.flags.BOX_RETURN_TUPLE,  -- flags
                        field_count, ...))
        end,

        -- insert a tuple (produces an error if the tuple already exists)
        insert = function(self, space, ...)
            local field_count = select('#', ...)
            return self:process(box.net.INSERT,
                   box.pack('iiV',
                        space,
                        bit.bor(box.flags.BOX_RETURN_TUPLE,
                                box.flags.BOX_ADD),  -- flags
                        field_count, ...))
        end,

        -- update a tuple
        update = function(self, space, key, format, ...)
            local op_count = bit.rshift(select('#', ...), 1)
            return self:process(box.net.UPDATE,
                   box.pack('iiVi'..format,
                        space,
                        box.flags.BOX_RETURN_TUPLE,
                        1, key,
                        op_count,
                        ...))
        end,

        select_limit = function(self, space, index, offset, limit, ...)
            local key_part_count = select('#', ...)
            return self:process(box.net.SELECT,
                   box.pack('iiiiiV',
                         space,
                         index,
                         offset,
                         limit,
                         1, -- key count
                         key_part_count, ...))
        end,

        select = function(self, space, index, ...)
            local key_part_count = select('#', ...)
            return self:process(box.net.SELECT,
                    box.pack('iiiiiV',
                         space,
                         index,
                         0, -- offset
                         4294967295, -- limit
                         1, -- key count
                         key_part_count, ...))
        end,


        ping = function(self)
            return self:process(box.net.PING, '')
        end,

        call    = function(self, proc_name, ...)
            assert(type(proc_name) == 'string')
            local count = select('#', ...)
            return self:process(box.net.CALL,
                box.pack('ipV',
                    0,                      -- flags
                    proc_name,
                    count,
                    ...))
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
        reconnect_timeout = tonumber(reconnect_timeout)
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
            request = box.pack('iiia', op, string.len(request), sync, request)


            if timeout ~= nil then
                timeout = tonumber(timeout)
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
                if op == box.net.PING then
                    return false
                else
                    return nil
                end
            end

            -- results { status, response } received
            if res[1] then
                if op == box.net.PING then
                    return true
                else
                    local rop, blen, sync, code, body =
                        box.unpack('iiiia', res[2])
                    if code ~= 0 then
                        box.raise(code, body)
                    end

            -- box.unpack('R') unpacks response body for us (tuple)
                    return box.unpack('R', body)
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
            local res = { self.s:recv(12) }
            if res[4] ~= nil then
                self:fatal("Can't read socket: %s", res[3])
                return
            end
            local header = res[1]
            if string.len(header) ~= 12 then
                self:fatal("Unexpected eof while reading header")
                return
            end

            local op, blen, sync = box.unpack('iii', header)

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
            return sync, header .. body
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
                    local sync, resp = self:read_response()
                    if sync == nil then
                        break
                    end

                    if self.processing[sync] ~= nil then
                        self.processing[sync]:put({true, resp}, 0)
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
