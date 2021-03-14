-- fio.lua (internal file)

local fio = require('fio')
local ffi = require('ffi')
local buffer = require('buffer')
local fiber = require('fiber')
local errno = require('errno')
local schedule_task = fiber._internal.schedule_task
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

ffi.cdef[[
    int umask(int mask);
    char *dirname(char *path);
    int chdir(const char *path);

    struct fio_handle {
        int fh;
    };
]]

local const_char_ptr_t = ffi.typeof('const char *')

local internal = fio.internal
fio.internal = nil

local function sprintf(fmt, ...)
    if select('#', ...) == 0 then
        return fmt
    end
    return string.format(fmt, ...)
end

local fio_methods = {}

-- read() -> str
-- read(buf) -> len
-- read(size) -> str
-- read(buf, size) -> len
fio_methods.read = function(self, buf, size)
    local tmpbuf
    if (not ffi.istype(const_char_ptr_t, buf) and buf == nil) or
        (ffi.istype(const_char_ptr_t, buf) and size == nil) then
        local st, err = self:stat()
        if st == nil then
            return nil, err
        end
        size = st.size
    end
    if not ffi.istype(const_char_ptr_t, buf) then
        size = buf or size
        tmpbuf = buffer.ibuf()
        buf = tmpbuf:reserve(size)
    end
    local res, err = internal.read(self.fh, buf, size)
    if res == nil then
        if tmpbuf ~= nil then
            tmpbuf:recycle()
        end
        return nil, err
    end
    if tmpbuf ~= nil then
        tmpbuf:alloc(res)
        res = ffi.string(tmpbuf.rpos, tmpbuf:size())
        tmpbuf:recycle()
    end
    return res
end

-- write(str)
-- write(buf, len)
fio_methods.write = function(self, data, len)
    if not ffi.istype(const_char_ptr_t, data) then
        data = tostring(data)
        len = #data
    end
    local res, err = internal.write(self.fh, data, len)
    if err ~= nil then
        return false, err
    end
    return res >= 0
end

-- pwrite(str, offset)
-- pwrite(buf, len, offset)
fio_methods.pwrite = function(self, data, len, offset)
    if not ffi.istype(const_char_ptr_t, data) then
        data = tostring(data)
        offset = len
        len = #data
    end
    local res, err = internal.pwrite(self.fh, data, len, offset)
    if err ~= nil then
        return false, err
    end
    return res >= 0
end

-- pread(size, offset) -> str
-- pread(buf, size, offset) -> len
fio_methods.pread = function(self, buf, size, offset)
    local tmpbuf
    if not ffi.istype(const_char_ptr_t, buf) then
        offset = size
        size = buf
        tmpbuf = buffer.ibuf()
        buf = tmpbuf:reserve(size)
    end
    local res, err = internal.pread(self.fh, buf, size, offset)
    if res == nil then
        if tmpbuf ~= nil then
            tmpbuf:recycle()
        end
        return nil, err
    end
    if tmpbuf ~= nil then
        tmpbuf:alloc(res)
        res = ffi.string(tmpbuf.rpos, tmpbuf:size())
        tmpbuf:recycle()
    end
    return res
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
            error(sprintf("fio.seek(): unknown whence: %s", whence))
        end
        whence = fio.c.seek[whence]
    else
        whence = tonumber(whence)
    end

    local res = internal.lseek(self.fh, tonumber(offset), whence)
    return tonumber(res)
end

fio_methods.close = function(self)
    local res, err = internal.close(self.fh)
    if err ~= nil then
        return false, err
    end
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

fio_methods.__serialize = function(self)
    return {fh = self.fh}
end

local fio_mt = {
    __index = fio_methods,
    __gc = function(obj)
        if obj.fh >= 0 then
            -- FFI GC can't yield. Internal.close() yields.
            -- Collect the garbage later, in a worker fiber.
            schedule_task(internal.close, obj.fh)
        end
    end,
}

ffi.metatype('struct fio_handle', fio_mt)

fio.open = function(path, flags, mode)
    local iflag = 0
    local imode = 0
    if type(path) ~= 'string' then
        error("Usage: fio.open(path[, flags[, mode]])")
    end
    if type(flags) ~= 'table' then
        flags = { flags }
    end
    if type(mode) ~= 'table' then
        mode = { mode or (bit.band(0x1FF, fio.umask())) }
    end


    for _, flag in pairs(flags) do
        if type(flag) == 'number' then
            iflag = bit.bor(iflag, flag)
        else
            if fio.c.flag[ flag ] == nil then
                error(sprintf("fio.open(): unknown flag: %s", flag))
            end
            iflag = bit.bor(iflag, fio.c.flag[ flag ])
        end
    end

    for _, m in pairs(mode) do
        if type(m) == 'string' then
            if fio.c.mode[m] == nil then
                error(sprintf("fio.open(): unknown mode: %s", m))
            end
            imode = bit.bor(imode, fio.c.mode[m])
        else
            imode = bit.bor(imode, tonumber(m))
        end
    end

    local fh, err = internal.open(tostring(path), iflag, imode)
    if err ~= nil then
        return nil, err
    end
    local ok, res = pcall(ffi.new, 'struct fio_handle', fh)
    if not ok then
        internal.close(fh)
        -- This is OOM.
        return error(res)
    end
    return res
end

fio.pathjoin = function(...)
    local i, path = 1, nil

    local len = select('#', ...)
    while i <= len do
        local sp = select(i, ...)
        if sp == nil then
            error("fio.pathjoin(): undefined path part "..i)
        end

        sp = tostring(sp)
        if sp ~= '' then
            path = sp
            break
        else
            i = i + 1
        end
    end

    if path == nil then
        return '.'
    end

    i = i + 1
    while i <= len do
        local sp = select(i, ...)
        if sp == nil then
            error("fio.pathjoin(): undefined path part "..i)
        end

        sp = tostring(sp)
        if sp ~= '' then
            path = path .. '/' .. sp
        end

        i = i + 1
    end

    path = path:gsub('/+', '/')
    if path ~= '/' then
        path = path:gsub('/$', '')
    end

    return path
end

fio.basename = function(path, suffix)
    if type(path) ~= 'string' then
        error("Usage: fio.basename(path[, suffix])")
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
    if type(path) ~= 'string' then
        error("Usage: fio.dirname(path)")
    end
    -- Can't just cast path to char * - on Linux dirname modifies
    -- its argument.
    local bsize = #path + 1
    local ibuf = cord_ibuf_take()
    local cpath = ibuf:alloc(bsize)
    ffi.copy(cpath, ffi.cast('const char *', path), bsize)
    path = ffi.string(ffi.C.dirname(cpath))
    cord_ibuf_put(ibuf)
    return path
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
        error("Usage: fio.abspath(path)")
    end
    path = path
    local joined_path
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
    if type(path)~='string' then
        error("Usage: fio.chdir(path)")
    end
    return ffi.C.chdir(path) == 0
end

fio.listdir = function(path)
    if type(path) ~= 'string' then
        error("Usage: fio.listdir(path)")
    end
    local str, err = internal.listdir(path)
    if err ~= nil then
        return nil, string.format("can't listdir %s: %s", path, err)
    end
    local t = {}
    if str == "" then
        return t
    end
    local names = string.split(str, "\n")
    for _, name in ipairs(names) do
        table.insert(t, name)
    end
    return t
end

fio.mktree = function(path, mode)
    if type(path) ~= "string" then
        error("Usage: fio.mktree(path[, mode])")
    end
    path = fio.abspath(path)

    local path = string.gsub(path, '^/', '')
    local dirs = string.split(path, "/")

    local current_dir = "/"
    for _, dir in ipairs(dirs) do
        current_dir = fio.pathjoin(current_dir, dir)
        local stat = fio.stat(current_dir)
        if stat == nil then
            local _, err = fio.mkdir(current_dir, mode)
            -- fio.stat() and fio.mkdir() above are separate calls
            -- and a file system may be changed between them. So
            -- if the error here is due to an existing directory,
            -- the function should not report an error.
            if err ~= nil and not fio.path.is_dir(current_dir) then
                return false, string.format("Error creating directory %s: %s",
                    current_dir, tostring(err))
            end
        elseif not stat:is_dir() then
            return false, string.format("Error creating directory %s: %s",
                current_dir, errno.strerror(errno.EEXIST))
        end
    end
    return true
end

fio.rmtree = function(path)
    if type(path) ~= 'string' then
        error("Usage: fio.rmtree(path)")
    end
    path = fio.abspath(path)
    local ls, err = fio.listdir(path)
    if err ~= nil then
        return nil, err
    end
    for _, f in ipairs(ls) do
        local tmppath = fio.pathjoin(path, f)
        local st = fio.lstat(tmppath)
        if st then
            if st:is_dir() then
                _, err = fio.rmtree(tmppath)
            else
                _, err = fio.unlink(tmppath)
            end
            if err ~= nil  then
                return nil, err
            end
        end
    end
    local _, err = fio.rmdir(path)
    if err ~= nil then
        return false, string.format("failed to remove %s: %s", path, err)
    end
    return true
end

fio.copyfile = function(from, to)
    if type(from) ~= 'string' or type(to) ~= 'string' then
        error('Usage: fio.copyfile(from, to)')
    end
    local st = fio.stat(to)
    if st and st:is_dir() then
        to = fio.pathjoin(to, fio.basename(from))
    end
    local _, err = internal.copyfile(from, to)
    if err ~= nil then
        return false, string.format("failed to copy %s to %s: %s", from, to, err)
    end
    return true
end

fio.copytree = function(from, to)
    if type(from) ~= 'string' or type(to) ~= 'string' then
        error('Usage: fio.copytree(from, to)')
    end
    local st = fio.stat(from)
    if not st then
        return false, string.format("Directory %s does not exist", from)
    end
    if not st:is_dir() then
        return false, errno.strerror(errno.ENOTDIR)
    end
    local ls, err = fio.listdir(from)
    if err ~= nil then
        return false, err
    end

    -- create tree of destination
    local _, reason = fio.mktree(to)
    if reason ~= nil then
        return false, reason
    end
    for _, f in ipairs(ls) do
        local ffrom = fio.pathjoin(from, f)
        local fto = fio.pathjoin(to, f)
        local st = fio.lstat(ffrom)
        if st and st:is_dir() then
            _, reason = fio.copytree(ffrom, fto)
            if reason ~= nil then
                return false, reason
            end
        end
        if st:is_reg() then
            _, reason = fio.copyfile(ffrom, fto)
            if reason ~= nil then
                return false, reason
            end
        end
        if st:is_link() then
            local link_to, reason = fio.readlink(ffrom)
            if reason ~= nil then
                return false, reason
            end
            _, reason = fio.symlink(link_to, fto)
            if reason ~= nil then
                return false, "can't create symlink in place of existing file "..fto
            end
        end
    end
    return true
end

local function check_time(time, name)
    if time ~= nil and type(time) ~= 'number' then
        error('fio.utime: ' .. name .. ' should be a number', 2)
    end
end

fio.utime = function(path, atime, mtime)
    if type(path) ~= 'string' then
        error('Usage: fio.utime(filepath[, atime[, mtime]])')
    end

    check_time(atime, 'atime')
    check_time(mtime, 'mtime')

    local current_time = fiber.time()
    atime = atime or current_time
    mtime = mtime or atime

    return internal.utime(path, atime, mtime)
end

fio.path = {}
fio.path.is_file = function(filename)
    local fs = fio.stat(filename)
    return fs ~= nil and fs:is_reg() or false
end

fio.path.is_link = function(filename)
    local fs = fio.lstat(filename)
    return fs ~= nil and fs:is_link() or false
end

fio.path.is_dir = function(filename)
    local fs = fio.stat(filename)
    return fs ~= nil and fs:is_dir() or false
end

fio.path.exists = function(filename)
    return fio.stat(filename) ~= nil
end

fio.path.lexists = function(filename)
    return fio.lstat(filename) ~= nil
end

return fio
