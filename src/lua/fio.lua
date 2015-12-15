-- fio.lua (internal file)

local fio = require('fio')
local ffi = require('ffi')

ffi.cdef[[
    int umask(int mask);
    char *dirname(char *path);
]]

local internal = fio.internal
fio.internal = nil

local function sprintf(fmt, ...)
    if select('#', ...) == 0 then
        return fmt
    end
    return string.format(fmt, ...)
end

local fio_methods = {}

fio_methods.read = function(self, size)
    if size == nil then
        return ''
    end

    return internal.read(self.fh, tonumber(size))
end

fio_methods.write = function(self, data)
    data = tostring(data)
    local res = internal.write(self.fh, data, #data)
    return res >= 0
end

fio_methods.pwrite = function(self, data, offset)
    data = tostring(data)
    local len = #data
    if len == 0 then
        return true
    end

    if offset == nil then
        offset = 0
    else
        offset = tonumber(offset)
    end

    local res = internal.pwrite(self.fh, data, len, offset)
    return res >= 0
end

fio_methods.pread = function(self, len, offset)
    if len == nil then
        return ''
    end
    if offset == nil then
        offset = 0
    end

    return internal.pread(self.fh, tonumber(len), tonumber(offset))
end


fio_methods.truncate = function(self, length)
    if length == nil then
        length = 0
    end
    return internal.ftruncate(self.fh, length)
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

    local res = internal.lseek(self.fh, tonumber(offset), whence)

    if res < 0 then
        return nil
    end
    return tonumber(res)
end

fio_methods.close = function(self)
    return internal.close(self.fh)
end

fio_methods.fsync = function(self)
    return internal.fsync(self.fh)
end

fio_methods.fdatasync = function(self)
    return internal.fdatasync(self.fh)
end


fio_methods.stat = function(self)
    return internal.fstat(self.fh)
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

    local fh = internal.open(tostring(path), iflag, imode)
    if fh < 0 then
        return nil
    end

    fh = { fh = fh }
    setmetatable(fh, fio_mt)
    return fh
end

-- Create a pathjoin(...) function
-- pythonic==false: pathjoin('/foo', '/bar', 'dummy') -> '/foo/bar/dummy'
-- pythonic==true:  pathjoin('/foo', '/bar', 'dummy') -> '/bar/dummy'
local function pathjoin_factory(pythonic) return function(...)
    local path = {}
    local absp = ''
    for i = 1, select('#', ...) do
        local sp = select(i, ...)
        sp = tostring(sp or '')
        if string.sub(sp,1,1)=='/' and (pythonic or i==1) then
            path = {}
            absp = '/'
        end
        for sp in string.gmatch(sp, '[^/]+') do
           if sp=='..' then
               if table.remove(path)==nil and absp=='' then
                   table.insert(path, "..")
               end
           elseif sp~='.' then
               table.insert(path, sp)
           end
        end
    end
    local res = absp..table.concat(path, '/')
    return res~='' and res or '.'
end end

fio.pathjoin = pathjoin_factory(false)
fio.pathjoin2 = pathjoin_factory(true)

fio.basename = function(path, suffix)
    if path == nil then
        return nil
    end

    path = tostring(path)
    path = string.gsub(path, '.*/', '')

    if suffix ~= nil then
        suffix = tostring(suffix)
        if #suffix > 0 then
            suffix = string.gsub(suffix, '(.)', '[%1]')
            path = string.gsub(path, suffix, '')
        end
    end

    return path
end

fio.dirname = function(path)
    if path == nil then
        return nil
    end
    path = tostring(path)
    path = ffi.new('char[?]', #path + 1, path)
    return ffi.string(ffi.C.dirname(path))
end

fio.umask = function(umask)

    if umask == nil then
        local old = ffi.C.umask(0)
        ffi.C.umask(old)
        return old
    end

    umask = tonumber(umask)

    return ffi.C.umask(tonumber(umask))

end

fio.abspath = function(path)
    -- following established conventions of fio module:
    -- letting nil through and converting path to string
    if path == nil then
        return nil
    end
    path = tostring(path)
    if string.sub(path, 1, 1) == '/' then
        return path
    else
        return fio.pathjoin(fio.cwd(), path)
    end
end

return fio
