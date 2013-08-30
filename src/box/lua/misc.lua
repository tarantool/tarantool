-- misc.lua (internal file)

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

-- This function automatically called by console client
-- on help command.
function help()
	return "server admin commands", {
		"box.snapshot()",
		"box.info()",
		"box.stat()",
		"box.slab.info()",
		"box.slab.check()",
		"box.fiber.info()",
		"box.plugin.info()",
		"box.errinj.info()",
		"box.errinj.set()",
		"box.cfg()",
		"box.cfg_reload()",
		"box.coredump()"
	}
end

-- This function automatically called by the server for
-- any new admin client.
function motd()
	return "Tarantool " .. box.info.version,
	       "Uptime: " .. box.info.uptime
end

-- vim: set et ts=4 sts
