local luatest = require('luatest')
local tarantool = require('tarantool')

-- Returns true if Tarantool build type is Debug.
local function is_debug_build()
    return tarantool.build.target:endswith('-Debug')
end

-- Returns true if Tarantool package is Enterprise.
local function is_enterprise_package()
    return tarantool.package == 'Tarantool Enterprise'
end

-- Skips a running test unless Tarantool build type is Debug.
local function skip_if_not_debug()
    luatest.skip_if(not is_debug_build(), 'build type is not Debug')
end

-- Skips a running test if Tarantool package is Enterprise.
local function skip_if_enterprise()
    luatest.skip_if(is_enterprise_package(), 'package is Enterprise')
end

return {
    skip_if_not_debug = skip_if_not_debug,
    skip_if_enterprise = skip_if_enterprise,
}
