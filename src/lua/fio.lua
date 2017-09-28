-- fio.lua (internal file)

local fio = require('fio')
local ffi = require('ffi')

ffi.cdef[[
    int umask(int mask);
    char *dirname(char *path);
    int chdir(const char *path);
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
    local res = internal.close(self.fh)
    self.fh = -1
    return res
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
        mode = { mode or 0x1FF } -- 0777
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

fio.pathjoin = function(path, ...)
    path = tostring(path)
    if path == nil or path == '' then
        error("Empty path part")
    end
    for i = 1, select('#', ...) do
        if string.match(path, '/$') ~= nil then
            path = string.gsub(path, '/$', '')
        end

        local sp = select(i, ...)
        if sp == nil then
            error("Undefined path part")
        end
        if sp == '' or sp == '/' then
            error("Empty path part")
        end
        if string.match(sp, '^/') ~= nil then
            sp = string.gsub(sp, '^/', '')
        end
        if sp ~= '' then
            path = path .. '/' .. sp
        end
    end
    if string.match(path, '/$') ~= nil and #path > 1 then
        path = string.gsub(path, '/$', '')
    end

    return path
end

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
    local joined_path = ''
    local path_tab = {}
    if string.sub(path, 1, 1) == '/' then
        joined_path = path
    else
        joined_path = fio.pathjoin(fio.cwd(), path)
    end
    for sp in string.gmatch(joined_path, '[^/]+') do
        if sp == '..' then
            table.remove(path_tab)
        elseif sp ~= '.' then
            table.insert(path_tab, sp)
        end
    end
    return '/' .. table.concat(path_tab, '/')
end

fio.chdir = function(path)
    if path == nil or type(path)~='string' then
        return false
    end
    return ffi.C.chdir(path) == 0
end

fio.listdir = function(path)
    if path == nil or type(path) ~= 'string' then
        return nil
    end
    local str = internal.listdir(path)
    if str == nil then
        return nil
    end
    local t = {}
    if str == "" then
        return t
    end
    local names = string.split(str, "\n")
    for i, name in ipairs(names) do
        table.insert(t, name)
    end
    return t
end

fio.mktree = function(path, mode)
    path = fio.abspath(path)
    if path == nil then
        return false
    end
    local path = string.gsub(path, '^/', '')
    local dirs = string.split(path, "/")

    if #dirs == 1 then
        if mode then
            return fio.mkdir(path, mode)
        else
            return fio.mkdir(path)
        end
    end

    local current_dir = "/"
    for i, dir in ipairs(dirs) do
        current_dir = fio.pathjoin(current_dir, dir)
        if not fio.stat(current_dir) then
            local res
            if mode then
                if not fio.mkdir(current_dir, mode) then
                    res = false
                end
            else
                if not fio.mkdir(current_dir) then
                    res = false
                end
            end
        end
    end
    return true
end

fio.rmtree = function(path)
    if path == nil then
        return false
    end
    local path = tostring(path)
    path = fio.abspath(path)
    local ls = fio.listdir(path)

    for i, f in ipairs(ls) do
        local tmppath = fio.pathjoin(path, f)
        if fio.stat(tmppath):is_dir() then
            if not fio.rmtree(tmppath) then
                return false
            end
        end
    end
    return fio.rmdir(path)
end

fio.copyfile = function(from, to)
    if type(from) ~= 'string' or type(to) ~= 'string' then
        error('Usage: fio.copyfile(from, to)')
    end
    local st = fio.stat(to)
    if st and st:is_dir() then
        to = fio.pathjoin(to, fio.basename(from))
    end
    return internal.copyfile(from, to)
end

fio.copytree = function(from, to)
    if type(from) ~= 'string' or type(to) ~= 'string' then
        error('Usage: fio.copytree(from, to)')
    end
    local status, reason
    from = fio.abspath(from)
    local st = fio.stat(from)
    if st == nil or not st:is_dir() then
        return false, errno.strerror(errno.ENOTDIR)
    end
    local ls = fio.listdir(from)
    to = fio.abspath(to)

    -- create tree of destination
    local status, reason = fio.mktree(to)
    if not status then
        return status, reason
    end
    for i, f in ipairs(ls) do
        local ffrom = fio.pathjoin(from, f)
        local fto = fio.pathjoin(to, f)
        local st = fio.lstat(ffrom)
        if st:is_dir() then
            status, reason = fio.copytree(ffrom, fto)
            if not status then
                return status, reason
            end
        end
        if st:is_reg() then
            status, reason = fio.copyfile(ffrom, fto)
            if not status then
                return status, reason
            end
        end
        if st:is_link() then
            local link_to, reason = fio.readlink(ffrom)
            if not link_to then
                return status, reason
            end
            status, reason = fio.symlink(link_to, fto)
            if not status then
                return nil, "can't create symlink in place of existing file "..fto
            end
        end
    end
    return true
end

return fio
