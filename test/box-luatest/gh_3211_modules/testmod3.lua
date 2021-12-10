old_require_testmod3 = require
require = function(modname)
    print('modname require testmod3 = ', modname)
    local module = old_require_testmod3(modname)
    return module
end

local log = require("log")

local function make_logs()
    log.info("info message from testmod3")
    log.error("error message from testmod3")
    log.debug("debug message from testmod3")
end


local testmod3 = {
    make_logs       = make_logs,
}

return testmod3
