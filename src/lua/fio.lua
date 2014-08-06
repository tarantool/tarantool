local ffi = require 'ffi'
local log = require 'log'

local fio = {}

ffi.cdef[[
    
    typedef ssize_t off_t;

    int open(const char *pathname, int flags, int mode);
    int unlink(const char *pathname);
    int close(int fd);

    int fsync(int fd);
    int fdatasync(int fd);
    ssize_t readlink(const char *pathname, char *buf, size_t bufsiz);
    int symlink(const char *target, const char *linkpath);
    int link(const char *oldpath, const char *newpath);
    int rename(const char *oldpath, const char *newpath);
    off_t lseek(int fd, off_t offset, int whence);
    
    ssize_t read(int fd, void *buf, size_t count);
    ssize_t write(int fd, const void *buf, size_t count);

    int truncate(const char *path, off_t length);
    int ftruncate(int fd, off_t length);
]]

local bsize = 4096
local buffer = ffi.new('char[?]', bsize)

local function sprintf(fmt, ...)
    if select('#', ...) == 0 then
        return fmt
    end
    return string.format(fmt, ...)
end

local fio_methods = {}


fio_methods.read = function(self, size)
    local b = ffi.new('char[?]', size)
    if b == nil then
        return nil
    end
    local res = ffi.C.read(self.fh, b, size)
    if res < 0 then
        return nil
    end
    return ffi.string(b, res)
end

fio_methods.write = function(self, data)
    data = tostring(data)
    local res = ffi.C.write(self.fh, data, #data)
--     local res = fio.internal.write(self.fh, data, #data)
    return res >= 0
end

fio_methods.pwrite = function(self, data, len, offset)
    data = tostring(data)
    if len == nil then
        len = #data
    end
    if offset == nil then
        offset = 0
    else
        offset = tonumber(offset)
    end

    local res = fio.internal.pwrite(self.fh, data, len, offset)
    return res >= 0
end

fio_methods.pread = function(self, len, offset)
    if len == nil then
        return ''
    end
    if offset == nil then
        offset = 0
    end

    return fio.internal.pread(self.fh, tonumber(len), tonumber(offset))
end


fio_methods.truncate = function(self, length)
    local res = ffi.C.ftruncate(self.fh, length)
    if res < 0 then
        return nil
    end
    return true
end

fio_methods.seek = function(self, offset, whence)
    if whence == nil then
        whence = 'SEEK_SET'
    end
    if type(whence) == 'string' then
        if fio.c.seek[whence] == nil then
            error(sprintf("Unknown whence: %s", whence))
        end
        whence = fio.c.seek[whence]
    else
        whence = tonumber(whence)
    end

    local res = ffi.C.lseek(self.fh, tonumber(offset), whence)

    if res < 0 then
        return nil
    end
    return tonumber(res)
end

fio_methods.close = function(self)
    local res = ffi.C.close(self.fh)
    if res < 0 then
        return false
    end
    return true
end

fio_methods.fsync = function(self)
    local res = ffi.C.fsync(self.fh)

    if res < 0 then
        return false
    end
    return true
end

fio_methods.fdatasync = function(self)
    local res = ffi.C.fdatasync(self.fh)

    if res < 0 then
        return false
    end
    return true
end


fio_methods.stat = function(self)
    return fio.fstat(self.fh)
end


local fio_mt = { __index = fio_methods }

fio.open = function(path, flags, mode)
    local iflag = 0
    local imode = 0

    if type(flags) ~= 'table' then
        flags = { flags }
    end
    if type(mode) ~= 'table' then
        mode = { mode }
    end


    for _, flag in pairs(flags) do
        if type(flag) == 'number' then
            iflag = bit.bor(iflag, flag)
        else
            if fio.c.flag[ flag ] == nil then
                error(sprintf("Unknown flag: %s", flag))
            end
            iflag = bit.bor(iflag, fio.c.flag[ flag ])
        end
    end

    for _, m in pairs(mode) do
        if type(m) == 'string' then
            if fio.c.mode[m] == nil then
                error(sprintf("Unknown mode: %s", m))
            end
            imode = bit.bor(imode, fio.c.mode[m])
        else
            imode = bit.bor(imode, tonumber(m))
        end
    end

    log.info("File: %s flag: %s, mode: %s", path, iflag, imode)

    
    local fh = fio.internal.open(tostring(path), iflag, imode)
    if fh < 0 then
        return nil
    end

    fh = { fh = fh }
    setmetatable(fh, fio_mt)
    return fh
end

fio.readlink = function(path)
    local res = ffi.C.readlink(tostring(path), buffer, bsize)
    if res < 0 then
        return nil
    end
    return ffi.string(buffer, res)
end

fio.symlink = function(path, linkpath)
    local res = ffi.C.symlink(tostring(path), tostring(linkpath))
    if res < 0 then
        return false
    end
    return true
end

fio.link = function(path, newpath)
    local res = ffi.C.link(tostring(path), tostring(linkpath))
    if res < 0 then
        return false
    end
    return true
end

fio.unlink = function(path)
    local res = ffi.C.unlink(tostring(path))
    if res == 0 then
        return true
    end
    return false
end

fio.rename = function(path, newpath)
    local res = ffi.C.rename(path, newpath)
    if res < 0 then
        return false
    end
    return true
end


fio.truncate = function(path, length)
    local res = ffi.C.truncate(tostring(path), tonumber(length))
    if res < 0 then
        return false
    end
    return true
end

return fio
