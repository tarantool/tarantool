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
        tuple = box.update(space, key, {'+', cnt_index, 1})
        if tuple ~= nil then break end
        local data = {...}
        table.insert(data, 1)
        tuple = box.insert(space, unpack(data))
        if tuple ~= nil then break end
    end

    return tuple[cnt_index]
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
    if tuple[cnt_index] == 1 then
        box.delete(space, ...)
        return 0
    else
        tuple = box.update(space, key, {'-', cnt_index, 1})
        return tuple[cnt_index]
    end
end


-- Assumes that spaceno has a TREE (NUM) primary key
-- inserts a tuple after getting the next value of the
-- primary key and returns it back to the user
function box.auto_increment(spaceno, ...)
    spaceno = tonumber(spaceno)
    local max_tuple = box.space[spaceno].index[0].idx:max()
    local max = 0
    if max_tuple ~= nil then
        max = max_tuple[0]
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
		"box.cfg()",
		"box.cfg.reload()",
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
