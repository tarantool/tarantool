local ffi = require('ffi')
local errno_list = require('errno.lib')

ffi.cdef[[
    const char *tt_strerror(int errnum);
]]

local function strerror(errno)
    if errno == nil then
        errno = ffi.errno()
    end
    return ffi.string(ffi.C.tt_strerror(tonumber(errno)))
end

return setmetatable({
    strerror = strerror
}, {
    __index = errno_list,
    __newindex = function() error("Can't create new errno constants") end,
    __call = function(self, ...) return ffi.errno(...) end
})
