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
function box.counter.inc(spaceno, key)
    local cnt_index = #key
    local s = box.space[spaceno]

    local tuple
    while true do
        tuple = s:update(key, {{'+', cnt_index, 1}})
        if tuple ~= nil then break end
        local data = key
        table.insert(data, 1)
        tuple = s:insert(data)
        if tuple ~= nil then break end
    end
    return tuple[cnt_index]
end

--
-- Decrement counter identified by primary key.
-- Delete counter if it decreased to zero.
-- Returns updated value of the counter.
--
function box.counter.dec(spaceno, key)
    local cnt_index = #key
    local s = box.space[spaceno]

    local tuple = s:get(key)
    if tuple == nil then return 0 end
    if tuple[cnt_index] == 1 then
        s:delete(key)
        return 0
    else
        tuple = s:update(key, {{'-', cnt_index, 1}})
        return tuple[cnt_index]
    end
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
