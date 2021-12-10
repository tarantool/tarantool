-- luacheck: globals old_require_testmod_1
old_require_testmod_1 = require

require = function(modname)
    print('modname require 2 = ', modname)
    local module = old_require_testmod_1(modname)
    return module
end

-- luacheck: globals old_require_testmod_2
old_require_testmod_2 = require

require = function(modname)
    print('modname require 3 = ', modname)
    local module = old_require_testmod_2(modname)
    return module
end

-- luacheck: globals old_require_testmod_3
old_require_testmod_3 = require

require = function(modname)
    print('modname require 4 = ', modname)
    local module = old_require_testmod_3(modname)
    return module
end

local log = require("log")

local function make_logs()
    log.info("info message from testmod")
    log.error("error message from testmod")
    log.debug("debug message from testmod")
end


local testmod = {
    make_logs       = make_logs,
}

return testmod
