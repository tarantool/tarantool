-- misc.lua (internal file)

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
