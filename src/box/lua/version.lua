local mkversion = require('internal.mkversion')
local utils     = require('internal.utils')

local check_param = utils.check_param

local function version_new(major, minor, patch)
    check_param(major, 'major', 'number')
    check_param(minor, 'minor', 'number')
    check_param(patch, 'patch', 'number')
    return mkversion.new(major, minor, patch)
end

local function version_fromstr(version_str)
    check_param(version_str, 'version-str', 'string')
    return mkversion.from_string(version_str)
end

return setmetatable({
    new = version_new,
    fromstr = version_fromstr,
}, {
    __call = function(self, ...) return version_new(...) end,
})
