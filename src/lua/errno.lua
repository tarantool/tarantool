-- errno.lua (internal file)

do

local ffi = require 'ffi'

ffi.cdef[[
    char *strerror(int errnum);
    int errno_get();
    int errno_set(int new_errno);
]]

box.errno.strerror = function(errno)
    if errno == nil then
        errno = box.errno()
    end
    return ffi.string(ffi.C.strerror(tonumber(errno)))
end

setmetatable(box.errno, {
    __newindex  = function() error("Can't create new errno constants") end,
    __call = function(self, new_errno)
        local res
        if new_errno then
            return ffi.C.errno_set(new_errno)
        end
        return ffi.C.errno_get()
    end
})

end
