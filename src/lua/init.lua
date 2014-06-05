-- init.lua -- internal file

-- Override pcall to support Tarantool exceptions

local ffi = require('ffi')
ffi.cdef[[
char *
tarantool_error_message(void);
]]

local pcall_lua = pcall

local function pcall_wrap(status, ...)
    if status == false and ... == 'C++ exception' then
        return false, ffi.string(ffi.C.tarantool_error_message())
    end
    return status, ...
end
pcall = function(fun, ...)
    return pcall_wrap(pcall_lua(fun, ...))
end

dostring = function(s, ...)
    local chunk, message = loadstring(s)
    if chunk == nil then
        error(message, 2)
    end
    return chunk(...)
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
