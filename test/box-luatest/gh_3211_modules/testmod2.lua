old_require_testmod2 = require

require = function(modname)
    print('modname require 5 = ', modname)
    local module = old_require_testmod2(modname)
    return module
end

local testmod = require("test.box-luatest.gh_3211_modules.testmod")
local log = require("log")


local function make_logs()
    log.info("info message from testmod2")
    log.error("error message from testmod2")

    testmod.make_logs()
end


local testmod2 = {
    make_logs       = make_logs,
}

return testmod2
