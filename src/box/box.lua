box.flags = { BOX_RETURN_TUPLE = 0x01, BOX_ADD = 0x02, BOX_REPLACE = 0x04 }


box.net = { box = {} }
box.net.box.lib = {
    delete = function(self, space, ...)
        local key_part_count = select('#', ...)
        return self:process(21,
                box.pack('iiV',
                    space,
                    box.flags.BOX_RETURN_TUPLE,  -- flags
                    key_part_count, ...))
    end,

    replace = function(self, space, ...)
        local field_count = select('#', ...)
        return self:process(13,
                box.pack('iiV',
                    space,
                    box.flags.BOX_RETURN_TUPLE,  -- flags
                    field_count, ...))
    end,

    -- insert a tuple (produces an error if the tuple already exists)
    insert = function(self, space, ...)
        local field_count = select('#', ...)
        return self:process(13,
               box.pack('iiV',
                    space,
                    bit.bor(box.flags.BOX_RETURN_TUPLE,
                            box.flags.BOX_ADD),  -- flags
                    field_count, ...))
    end,

    -- update a tuple
    update = function(self, space, key, format, ...)
        local op_count = select('#', ...)/2
        return self:process(19,
               box.pack('iiVi'..format,
                    space,
                    box.flags.BOX_RETURN_TUPLE,
                    1, key,
                    op_count,
                    ...))
    end,

    select_limit = function(self, space, index, offset, limit, ...)
        local key_part_count = select('#', ...)
        return self:process(17,
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
        return self:process(17,
                box.pack('iiiiiV',
                     space,
                     index,
                     0, -- offset
                     4294967295, -- limit
                     1, -- key count
                     key_part_count, ...))
    end,


    ping = function(self)
        return self:process(65280, '')
    end,

    call    = function(self, proc_name, ...)
        local count = select('#', ...)
        return self:process(22,
            box.pack('iwaV',
                0,                      -- flags
                string.len(proc_name),
                proc_name,
                count,
                ...))
    end,

    select_range = function(self, sno, ino, limit, ...)
        return self:call(
            'box.select_range',
            tostring(sno),
            tostring(ino),
            tostring(limit),
            ...
        )
    end,

    select_reverse_range = function(self, sno, ino, limit, ...)
        return self:call(
            'box.select_reverse_range',
            tostring(sno),
            tostring(ino),
            tostring(limit),
            ...
        )
    end,


    timeout = function(self, timeout)

        local wrapper = {}

        setmetatable(wrapper, {
            __index = function(wrp, name, ...)
                local foo = self[name]
                if foo ~= nil then
                    return
                        function(wr, ...)
                            self.timeout_request = timeout
                            return foo(self, ...)
                        end
                end
                
                error(string.format('Can not find "remote:%s" function', name))
            end
        });

        return wrapper
    end,
}


-- local tarantool
box.net.box.emb = {
    process = function(self, ...)
        return box.process(...)
    end,
    
    select_range = function(self, sno, ino, limit, ...)
        return box.space[tonumber(sno)].index[tonumber(ino)]
            :select_range(tonumber(limit), ...)
    end,

    select_reverse_range = function(self, sno, ino, limit, ...)
        return box.space[tonumber(sno)].index[tonumber(ino)]
            :select_reverse_range(tonumber(limit), ...)
    end,

    call = function(self, proc_name, ...)
        local fref = _G
        for spath in string.gmatch(proc_name, '([^.]+)') do
            fref = fref[spath]
            if fref == nil then
                error("function '" .. proc_name .. "' was not found")
            end
        end
        if type(fref) ~= 'function' then
            error("object '" .. proc_name .. "' is not a function")
        end
        return fref(...)
    end,

    ping = function(self)
        return true
    end,
    
    -- local tarantool doesn't provide timeouts
    timeout = function(self, timeout)
        return self
    end,

    close = function(self)
        error("box.net.box.emb can't be closed")
    end
}

setmetatable(box.net.box.emb, { __index = box.net.box.lib })

box.net.box.new = function(host, port, timeout)

    local remote = {
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
            end
        },

        read_fiber = function(self)
            return function()
                box.fiber.detach()
                local res
                while true do
                    if self.s == nil then
                        box.fiber.sleep(.01)
                    else
                        -- TODO: error handling

                        res = { self.s:recv(12) }
                        if res[3] ~= nil then break end
                        local header = res[1]
                        if string.len(header) ~= 12 then break end
                        local op, blen, sync = box.unpack('iii', header)
                        
                        local body = ''
                        if blen > 0 then
                            res = { self.s:recv(blen) }
                            if res[3] ~= nil then break end
                            body = res[1]
                            if string.len(body) ~= blen then break end


                        end
                        if self.processing[sync] ~= nil then
                            local ch = self.processing[sync]
                            ch:put(header .. body, 0)
                        else
                            print("Unexpected response ", sync)
                        end
                    end
                end
                self:close()
            end
        end,


        connect_process = function(self, ...)
            self.s = box.socket.tcp()
            if self.s == nil then
                self:close("Can not create socket")
            end

            local timeout = self.timeout_request
            self.timeout_request = nil
            
            if self.cch ~= nil then
                if self.connect[3] == nil then
                    self.cch:get()
                else
                    self.cch:get(self.connect[3])
                end

                if self.o_read_fiber == nil then
                    local e = { self.s:error() }
                    error(e[2])
                end

            else
                local res = { self.s:connect( unpack(self.connect) ) }
                if self.cch ~= nil then
                    while self.cch:has_readers() do
                        self.cch:put(true, 0)
                    end
                end
                self.cch = nil


                if res[1] == nil then
                    self:close("Can't connect to remote tarantool")
                    error(res[4])
                end
                self.process = self.connected_process
                self.o_read_fiber = box.fiber.create( self.read_fiber(self) )
                box.fiber.resume(self.o_read_fiber)

            end


            self.timeout_request = timeout
            return self:connected_process(...)
        end,

        connected_process = function(self, op, request)
            local timeout = self.timeout_request
            self.timeout_request = nil
            local sync = self.processing:next_sync()
            self.processing[sync] = box.ipc.channel(1)
            request = box.pack('iiia', op, string.len(request), sync, request)


            local res = { self.s:send(request) }
            if res[3] ~= nil then
                self:close(res[3])
                error(res[3])
            end

            self.timeout_request = timeout
            return self:wait_response(sync, op)
        end,
        
        wait_response = function(self, sync, op)
            local response;
            if self.timeout_request ~= nil then
                response = self.processing[sync]
                    :get(tonumber(self.timeout_request))
            else
                response = self.processing[sync]:get()
            end
            self.processing[sync] = nil

            if response == false then
                self:close("Lost connection to remote tarantool")
                self.ping = function()
                    return false
                end
                if op == 65280 then
                    return false
                end
                self:process()  -- throw exception
            end

            if response == nil then
                return nil
            end

            if op == 65280 then
                return true
            end

            local rop, blen, sync, code, body = box.unpack('iiiia', response)
            if code ~= 0 then
                box.raise(code, body)
            end

            return box.unpack('R', body)
        end,
        
        close = function(self, message)
            if message == nil then
                message = "Connection isn't established"
            end
            self.process = function() error(message) end
            self.close = function()
                error("Remote connection is already closed")
            end
            self.ping = function()
                return false
            end

            for sync, ch in pairs(self.processing) do
                if type(sync) == 'number' then
                    ch:put(false, 0)
                end
                self.processing[sync] = nil
            end

            if self.s ~= nil then
                local s = self.s
                self.s = nil
                s:close()
            end
        end,


    }
    
    remote.connect = { host, port }
    if timeout ~= nil then
        table.insert(remote.connect, tonumber(timeout))
    end

    remote.process = remote.connect_process

    setmetatable(
        remote, {
            __index = box.net.box.lib,
            __gc = function(self)
                if self.s ~= nil then
                    self.s:close()
                    self.s = nil
                end
            end
        }
    )

    return remote
end


--
--
--
function box.call(proc_name, ...)
    return box.net.box.emb:call(proc_name, ...)
end

--
--
--
function box.select_limit(space, index, offset, limit, ...)
    return box.net.box.emb:select_limit(space, index, offset, limit, ...)
end


--
--
--
function box.select(space, index, ...)
    return box.net.box.emb:select(space, index, ...)
end

--
-- Select a range of tuples in a given namespace via a given
-- index. If key is NULL, starts from the beginning, otherwise
-- starts from the key.
--
function box.select_range(sno, ino, limit, ...)
    return box.net.box.emb:select_range(sno, ino, limit, ...)
end

--
-- Select a range of tuples in a given namespace via a given
-- index in reverse order. If key is NULL, starts from the end, otherwise
-- starts from the key.
--
function box.select_reverse_range(sno, ino, limit, ...)
    return box.net.box.emb:select_reverse_range(sno, ino, limit, ...)
end

--
-- delete can be done only by the primary key, whose
-- index is always 0. It doesn't accept compound keys
--
function box.delete(space, ...)
    return box.net.box.emb:delete(space, ...)
end

-- insert or replace a tuple
function box.replace(space, ...)
    return box.net.box.emb:replace(space, ...)
end

-- insert a tuple (produces an error if the tuple already exists)
function box.insert(space, ...)
    return box.net.box.emb:insert(space, ...)
end

--
function box.update(space, key, format, ...)
    return box.net.box.emb:update(space, key, format, ...)
end


function box.dostring(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
end

-- Assumes that spaceno has a TREE int32 (NUM) or int64 (NUM64) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
function box.auto_increment(spaceno, ...)
    spaceno = tonumber(spaceno)
    local max_tuple = box.space[spaceno].index[0].idx:max()
    local max = 0
    if max_tuple ~= nil then
        max = max_tuple[0]
        local fmt = 'i'
        if #max == 8 then fmt = 'l' end
        max = box.unpack(fmt, max)
    else
        -- first time
        if box.space[spaceno].index[0].key_field[0].type == "NUM64" then
            max = tonumber64(max)
        end
    end
    return box.insert(spaceno, max + 1, ...)
end

--
-- Simple counter.
--
box.counter = {}

--
-- Increment counter identified by primary key.
-- Create counter if not exists.
-- Returns updated value of the counter.
--
function box.counter.inc(space, ...)
    local key = {...}
    local cnt_index = #key

    local tuple
    while true do
        tuple = box.update(space, key, '+p', cnt_index, 1)
        if tuple ~= nil then break end
        local data = {...}
        table.insert(data, 1)
        tuple = box.insert(space, unpack(data))
        if tuple ~= nil then break end
    end

    return box.unpack('i', tuple[cnt_index])
end

--
-- Decrement counter identified by primary key.
-- Delete counter if it decreased to zero.
-- Returns updated value of the counter.
--
function box.counter.dec(space, ...)
    local key = {...}
    local cnt_index = #key

    local tuple = box.select(space, 0, ...)
    if tuple == nil then return 0 end
    if box.unpack('i', tuple[cnt_index]) == 1 then
        box.delete(space, ...)
        return 0
    else
        tuple = box.update(space, key, '-p', cnt_index, 1)
        return box.unpack('i', tuple[cnt_index])
    end
end

function box.bless_space(space)
    local index_mt = {}
    -- __len and __index
    index_mt.len = function(index) return #index.idx end
    index_mt.__newindex = function(table, index)
        return error('Attempt to modify a read-only table') end
    index_mt.__index = index_mt
    -- min and max
    index_mt.min = function(index) return index.idx:min() end
    index_mt.max = function(index) return index.idx:max() end
    index_mt.random = function(index, rnd) return index.idx:random(rnd) end
    -- iteration
    index_mt.iterator = function(index, ...)
        return index.idx:iterator(...)
    end
    --
    -- pairs/next/prev methods are provided for backward compatibility purposes only
    index_mt.pairs = function(index)
        return index.idx.next, index.idx, nil
    end
    --
    local next_compat = function(idx, iterator_type, ...)
        local arg = {...}
        if #arg == 1 and type(arg[1]) == "userdata" then
            return idx:next(...)
        else
            return idx:next(iterator_type, ...)
        end
    end
    index_mt.next = function(index, ...)
        return next_compat(index.idx, box.index.GE, ...);
    end
    index_mt.prev = function(index, ...)
        return next_compat(index.idx, box.index.LE, ...);
    end
    index_mt.next_equal = function(index, ...)
        return next_compat(index.idx, box.index.EQ, ...);
    end
    index_mt.prev_equal = function(index, ...)
        return next_compat(index.idx, box.index.REQ, ...);
    end
    -- index subtree size
    index_mt.count = function(index, ...)
        return index.idx:count(...)
    end
    --
    index_mt.select_range = function(index, limit, ...)
        local range = {}
        for v in index:iterator(box.index.GE, ...) do
            if #range >= limit then
                break
            end
            table.insert(range, v)
            iterator_state, v = index:next(iterator_state)
        end
        return unpack(range)
    end
    index_mt.select_reverse_range = function(index, limit, ...)
        local range = {}
        for v in index:iterator(box.index.LE, ...) do
            if #range >= limit then
                break
            end
            table.insert(range, v)
            iterator_state, v = index:prev(iterator_state)
        end
        return unpack(range)
    end
    --
    local space_mt = {}
    space_mt.len = function(space) return space.index[0]:len() end
    space_mt.__newindex = index_mt.__newindex
    space_mt.select = function(space, ...) return box.select(space.n, ...) end
    space_mt.select_range = function(space, ino, limit, ...)
        return space.index[ino]:select_range(limit, ...)
    end
    space_mt.select_reverse_range = function(space, ino, limit, ...)
        return space.index[ino]:select_reverse_range(limit, ...)
    end
    space_mt.select_limit = function(space, ino, offset, limit, ...)
        return box.select_limit(space.n, ino, offset, limit, ...)
    end
    space_mt.insert = function(space, ...) return box.insert(space.n, ...) end
    space_mt.update = function(space, ...) return box.update(space.n, ...) end
    space_mt.replace = function(space, ...) return box.replace(space.n, ...) end
    space_mt.delete = function(space, ...) return box.delete(space.n, ...) end
    space_mt.truncate = function(space)
        local pk = space.index[0]
        while #pk.idx > 0 do
            for t in pk:iterator() do
                local key = {};
                -- ipairs does not work because pk.key_field is zero-indexed
                for _k2, key_field in pairs(pk.key_field) do
                    table.insert(key, t[key_field.fieldno])
                end
                space:delete(unpack(key))
            end
        end
    end
    space_mt.pairs = function(space) return space.index[0]:pairs() end
    space_mt.__index = space_mt

    setmetatable(space, space_mt)
    if type(space.index) == 'table' and space.enabled then
        for j, index in pairs(space.index) do
            rawset(index, 'idx', box.index.new(space.n, j))
            setmetatable(index, index_mt)
        end
    end
end

-- User can redefine the hook
function box.on_reload_configuration()
end


local s
local syncno = 0

function iostart()
    if s ~= nil then
        return
    end

    s = box.socket.tcp()
    s:connect('127.0.0.1', box.cfg.primary_port)
    
    box.fiber.resume( box.fiber.create(
        function()
            box.fiber.detach()
            while true do
                s:recv(12)
            end
        end
    ))
end

function iotest()
    iostart()

    syncno = syncno + 1
    return s:send(box.pack('iii', 65280, 0, syncno))

end



require("bit")

-- vim: set et ts=4 sts
