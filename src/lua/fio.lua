-- fio.lua (internal file)

---@alias seek
---| 'SEEK_SET'
---| 'SEEK_DATA'
---| 'SEEK_HOLE'
---| 'SEEK_END'
---| 'SEEK_CUR'

---@alias mode
---| 'S_IWGRP'
---| 'S_IXGRP'
---| 'S_IROTH'
---| 'S_IXOTH'
---| 'S_IRUSR'
---| 'S_IXUSR'
---| 'S_IRWXU'
---| 'S_IRWXG'
---| 'S_IWOTH'
---| 'S_IRWXO'
---| 'S_IWUSR'
---| 'S_IRGRP'

---@alias flags
---| 'O_APPEND' # (start at end of file)
---| 'O_ASYNC' # (signal when IO is possible)
---| 'O_CLOEXEC' # (enable a flag related to closing)
---| 'O_CREAT' # (create file if it doesn't exist)
---| 'O_DIRECT' # (do less caching or no caching)
---| 'O_DIRECTORY' # (fail if it's not a directory)
---| 'O_EXCL' # (fail if file cannot be created)
---| 'O_LARGEFILE' # (allow 64-bit file offsets)
---| 'O_NDELAY'
---| 'O_NOATIME' # (no access-time updating)
---| 'O_NOCTTY' # (no console tty)
---| 'O_NOFOLLOW' # (no following symbolic links)
---| 'O_NONBLOCK' # (no blocking)
---| 'O_PATH' # (get a path for low-level use)
---| 'O_RDONLY' # (read only)
---| 'O_RDWR' # (either read or write)
---| 'O_SYNC' # (force writing if it's possible)
---| 'O_TMPFILE' # (the file will be temporary and nameless)
---| 'O_TRUNC' # (truncate)
---| 'O_WRONLY' # (write only)

---@class fio.stat: table
---@field inode number inode number.
---@field rdev number device ID.
---@field size number file size in bytes.
---@field atime number last access time.
---@field mode number file mode.
---@field mtime number last modification time.
---@field nlink number hard link count.
---@field uid number owner user ID.
---@field blksize number block size.
---@field gid number owner group ID.
---@field ctime number last status change time.
---@field dev number device ID.
---@field blocks number allocated block count.

---@class fio.file: ffi.cdata*
---@field close fun(self: fio.file): boolean, string? Close the file.
---@field pread fun(self: fio.file, ...): ... Perform random-access read.
---@field pwrite fun(self: fio.file, ...): ... Perform random-access write.
---@field read fun(self: fio.file, ...): ... Perform non-random-access read.
---@field write fun(self: fio.file, ...): ... Perform non-random-access write.
---@field truncate fun(self: fio.file, length: number): boolean, string? Set
---    the file size.
---@field seek fun(self: fio.file, offset: number, whence?: seek): number? Move
---    the position.
---@field stat fun(self: fio.file): fio.stat Get statistics about the open file.
---@field fsync fun(self: fio.file): boolean, string? Flush changes to disk.
---@field fdatasync fun(self: fio.file): boolean, string? Flush data to disk.

---@class fio.path
---@field exists fun(path: string): boolean Check if the path exists.
---@field is_dir fun(path: string): boolean Check if the path is a directory.
---@field is_file fun(path: string): boolean Check if the path is a file.
---@field is_link fun(path: string): boolean Check
---    if the path is a symbolic link.
---@field lexists fun(path: string): boolean Check
---    if the path exists or is a broken link.

---@alias fio.flags number|flags|flags[]

---@alias fio.error ffi.cdata*

---@class fio.constants
---@field seek table<string, integer>
---@field mode table<string, integer>
---@field flag table<string, integer>

---@class fio
---Concatenate partial string, separated by '/' to form a path name.
---
---**Example:** `fio.pathjoin('/etc', 'default', 'myfile')` =>
---`'/etc/default/myfile'`
---
---@field pathjoin fun(...: string): string
---Given a full path name, remove all but the final part (the file name).
---Also remove the suffix, if it is passed. Note that the basename of a path
---with a trailing slash is an empty string. It is different from how the
---Unix basename program interprets such a path.
---
---**Example:** `fio.basename('/path/to/my.lua', '.lua')` => `'my'`
---
---@field basename fun(path_name: string, suffix?: string): string
---Get a directory name.
---
---**Example:** `fio.dirname('/path/to/my.lua')` => `'/path/to/'`
---
---@field dirname fun(path_name: string): string
---Given a full path name, remove the final part (the file name).
---
---**Example:** `fio.abspath('my.lua')` => `'/path/to/my.lua'`
---
---@field abspath fun(file_name: string): string
---Get the name of the current working directory.
---
---**Example:** `fio.cwd()` => `'/home/username/tarantool_sandbox'`
---
---@field cwd fun(): string
---Get information about a file object.
---
---Returns information about a file object. For details type `man 2 lstat`
---or `man 2 stat`.
---
---**Example:**
---
--- `fio.lstat('/etc')`
---
--- ```yaml
---   inode: 1048577
---   rdev: 0
---   size: 12288
---   atime: 1421340698
---   mode: 16877
---   mtime: 1424615337
---   nlink: 160
---   uid: 0
---   blksize: 4096
---   gid: 0
---   ctime: 1424615337
---   dev: 2049
---   blocks: 24
--- ```
---
---@field lstat fun(path: string): fio.stat
---Get information about a file object.
---
---Returns information about a file object. For details type `man 2 lstat`
---or `man 2 stat`.
---
---@field stat fun(path: string): fio.stat
---Create a directory.
---
---**Example:** `fio.mkdir('/etc')` => `false`
---
---@field mkdir fun(path_name: string, mode?: number): boolean, string?
---Delete a directory.
---
---@field rmdir fun(path_name: string): boolean, string?
---Change working directory.
---
---**Example:** `fio.chdir('/etc')` => `true`
---
---@field chdir fun(path_name: string): boolean
---List files in a directory.
---
---The result is similar to the ls shell command.
---
---@field listdir fun(path_name: string): string[]?, string?
---Get files whose names match a given string.
---
---Return a list of files that match an input string. The list is
---constructed with a single flag that controls the behavior of the function:
---GLOB_NOESCAPE. For details type `man 3 glob`.
---
---**Example:**
---
--- `fio.glob('/etc/x*')`
--- ```
--- - - /etc/xdg
---   - /etc/xml
---   - /etc/xul-ext
--- ```
---
---@field glob fun(path_name: string): string[]
---Return the name of a directory that can be used to store temporary files.
---
---`fio.tempdir()` stores the created temporary directory into `/tmp` by
---default. *Since 2.4.1*, this can be changed by setting the `TMPDIR`
---environment variable. Before starting Tarantool, or at runtime by
---`os.setenv()`.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fio.tempdir()
--- ---
--- - /tmp/lG31e7
--- ...
--- tarantool> fio.mkdir('./mytmp')
--- ---
--- - true
--- ...
---
--- tarantool> os.setenv('TMPDIR', './mytmp')
--- ---
--- ...
---
--- tarantool> fio.tempdir()
--- ---
--- - ./mytmp/506Z0b
--- ```
---
---@field tempdir fun(): string
---Copy everything in the `from-path`, including subdirectory contents, to
---the `to-path`.
---
---The result is similar to the `cp -r` shell command. The `to-path` should
---not be empty.
---
---@field copytree fun(from_path: string, to_path: string): boolean, string?
---Create the path, including parent directories, but without file contents.
---
---The result is similar to the `mkdir -p` shell command.
---
---**Example:** `fio.mktree('/home/archives')` => `true`
---
---@field mktree fun(path_name: string, mode?: number): boolean, string?
---Delete directories.
---
---**Example:** `fio.rmtree('/home/archives')` => `true`
---
---@field rmtree fun(path_name: string): boolean?, string?
---Creates link.
---
---@field link fun(src: string, dst: string): boolean, string?
---Creates link.
---
---@field symlink fun(src: string, dst: string): boolean, string?
---Reads link.
---
---@field readlink fun(src: string): string
---Deletes link or file.
---
---@field unlink fun(path_name: string): boolean, string?
---Rename a file or directory.
---
---@field rename fun(path_name: string, new_path_name: string): boolean, string?
---Change the access time and possibly also change the update time of a file.
---
---For details type man 2 utime. Times should be expressed as number of
---seconds since the epoch.
---
---@field utime fun(name: string, acc?: number, mod?: number): boolean, string?
---Copy a file. The result is similar to the cp shell command.
---
---@field copyfile fun(from: string, to: string): boolean, string?
---Manage rights to and ownership of file objects.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fio.chown('/home/username/tmp.txt', 'username', 'username')
--- - true
--- ```
---
---@field chown fun(path: string, user: string, group: string): boolean, string?
---Manage rights to and ownership of file objects.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fio.chmod('/home/username/tmp.txt', tonumber('0755', 8))
--- - true
--- ```
---
---@field chmod fun(path_name: string, new_rights: number): boolean, string?
---Reduce the file size.
---
---@field truncate fun(path_name: string, new_size?: number): boolean, string?
---Ensure that changes are written to disk.
---
---@field sync fun(): boolean
---Open a file.
---
---@field open fun(p: string, f?: fio.flags, m?: number): fio.file?, fio.error?
---Table of functions for checking paths.
---
---@field path fio.path
---Table of constants which are the same as POSIX flag values.
---
---@field c fio.constants

local minifio = require('internal.minifio')
---Tarantool supports file input/output with an API that is similar to POSIX
---syscalls. All operations are performed asynchronously. Multiple fibers can
---access the same file simultaneously.
---
---* Functions for common pathname manipulations.
---* Functions for directory or file existence and type checks.
---* Functions for common file manipulations.
---* Constants which are the same as POSIX flag values (for example
---  `fio.c.flag.O_RDONLY` = POSIX O_RDONLY).
---
---@type fio
local fio = require('fio')
local ffi = require('ffi')
local buffer = require('buffer')
local fiber = require('fiber')
local errno = require('errno')
local schedule_task = fiber._internal.schedule_task
ffi.cdef[[
    int umask(int mask);
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
---Perform non-random-access read on a file.
---
---@overload fun(self: fio.file, count?: number): string
---@overload fun(self: fio.file, buffer: ffi.cdata*, count: number): number
---@param self fio.file
---@param buf ffi.cdata*|number|nil where to read into, or the number of
---    bytes to read.
---@param size? number bytes to read.
---@return string|number|nil result
---@return string? error_message
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
---Perform non-random-access write on a file.
---
---@overload fun(self: fio.file, buffer: ffi.cdata*, count: number): boolean
---@param self fio.file
---@param data string|ffi.cdata* value to write.
---@param len? number bytes to write.
---@return boolean success
---@return string? error_message
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
---Perform random-access write on a file.
---
---@overload fun(self: fio.file, b: ffi.cdata*, n: number, o: number): number
---@param self fio.file
---@param data string|ffi.cdata* value to write.
---@param len? number bytes to write.
---@param offset number offset within file where writing begins.
---@return boolean success, string? error_message
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
---Perform random-access read on a file.
---
---@overload fun(self: fio.file, b: ffi.cdata*, n: number, o: number): number
---@param self fio.file
---@param buf ffi.cdata*|number where to read into, or the number of bytes
---    to read.
---@param size number bytes to read.
---@param offset? number offset within file where reading begins.
---@return string|number|nil result
---@return string? error_message
fio_methods.pread = function(self, buf, size, offset)
    local tmpbuf
    local has_buf = ffi.istype(const_char_ptr_t, buf)
    if not has_buf then
        offset = size
        size = buf
    end
    if type(size) ~= 'number' or (type(offset) ~= 'number' and offset ~= nil) then
        error('Usage: fh:pread(buf, size[, offset]) or fh:pread(size[, offset])')
    end
    if not has_buf then
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

---Change the size of an open file.
---
---@param self fio.file
---@param length? number new file size. Default is 0.
---@return boolean success, string? error_message
fio_methods.truncate = function(self, length)
    if length == nil then
        length = 0
    end
    return internal.ftruncate(self.fh, length)
end

---Change position in a file.
---
---`SEEK_END` = end of file,
---
---`SEEK_CUR` = current position,
---
---`SEEK_SET` = start of file.
---
---@param self fio.file
---@param offset number position to seek to.
---@param whence? seek|integer offset from. Default is `SEEK_SET`.
---@return number? result the new position if success
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

---Close a file.
---
---@param self fio.file
---@return boolean success, string? error_message
fio_methods.close = function(self)
    local res, err = internal.close(self.fh)
    if err ~= nil then
        return false, err
    end
    self.fh = -1
    return res
end

---Ensure that changes made to an open file are written to disk.
---
---@param self fio.file
---@return boolean success, string? error_message
fio_methods.fsync = function(self)
    return internal.fsync(self.fh)
end

---Ensure that changes made to an open file are written to disk.
---
---@param self fio.file
---@return boolean success, string? error_message
fio_methods.fdatasync = function(self)
    return internal.fdatasync(self.fh)
end

---Get statistics about an open file.
---
---@param self fio.file
---@return fio.stat
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

---Open a file.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fh = fio.open('/home/username/tmp.txt', {'O_RDWR', 'O_APPEND'})
--- ---
--- ...
--- tarantool> fh -- display file handle returned by fio.open
--- ---
--- - fh: 11
--- ...
--- ```
---
---@param path string full path to the file to open.
---@param flags? number|flags|flags[] flags can be passed as a number or as
---    string constants, for example 'O_RDONLY', 'O_WRONLY', 'O_RDWR'. Flags
---    can be combined by enclosing them in braces.
---@param mode? number mode bits can be passed as a number or as string
---    constants, for example S_IWUSR. Mode bits can be combined by enclosing
---    them in braces.
---@return fio.file? fh the opened file handle.
---@return fio.error? err an error if the file could not be opened.
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
        mode = { mode or tonumber('666', 8) }
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
    ---@cast res fio.file
    return res
end

-- Expose several functions implemented as part of the minifio
-- module.
for name, func in pairs(minifio.expose_from_fio) do
    fio[name] = func
end

-- List of functions exposed from fio, but defined in minifio.
--
-- Without the list a reader should be aware of the fio/minifio
-- splitting in order to find an implementation of a function by
-- its name. Let's make a reader's life a bit easier.
assert(type(fio.cwd) == 'function')
assert(type(fio.pathjoin) == 'function')
assert(type(fio.abspath) == 'function')

---Get a file name.
---
---Given a full path name, remove all but the final part (the file name).
---Also remove the suffix, if it is passed.
---
---Note that the basename of a path with a trailing slash is an empty string.
---It is different from how the Unix basename program interprets such a path.
---
---**Example:**
---
---`fio.basename('/path/to/my.lua', '.lua')` => `'my'`
---
---@param path string path name
---@param suffix? string suffix
---@return string file_name
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

---Get a directory name.
---
---**Example:**
---
---`fio.dirname('/path/to/my.lua')` => `'/path/to/'`
---
---@param path string path name
---@return string
fio.dirname = function(path)
    if type(path) ~= 'string' then
        error("Usage: fio.dirname(path)")
    end
    return minifio.dirname(path)
end

---Set the mask bits used when creating files or directories.
---
---For a detailed description type `man 2 umask`.
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fio.umask(tonumber('755', 8))
--- ---
--- - 493
--- ...
--- ```
---
---@param umask? number mask bits.
---@return number result previous mask bits.
fio.umask = function(umask)

    if umask == nil then
        local old = ffi.C.umask(0)
        ffi.C.umask(old)
        return old
    end

    umask = tonumber(umask)

    return ffi.C.umask(tonumber(umask))

end

---Change working directory.
---
---**Example:**
---
---`fio.chdir('/etc')` => `true`
---
---@param path string path of directory.
---@return boolean success
fio.chdir = function(path)
    if type(path)~='string' then
        error("Usage: fio.chdir(path)")
    end
    return ffi.C.chdir(path) == 0
end

---List files in a directory.
---
---The result is similar to the ls shell command.
---
---@param path string path of directory.
---@return string[]|nil list files in the directory.
---@return string? error_message
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

---Create the path, including parent directories, but without file contents.
---
---The result is similar to the `mkdir -p` shell command.
---
---**Example:**
---
---`fio.mktree('/home/archives')` => `true`
---
---@param path string
---@param mode? number
---@return boolean success, string? error_message
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

---Delete directories.
---
---**Example:**
---
---`fio.rmtree('/home/archives')` => `true`
---
---@param path string
---@return boolean|nil success, string? error_message
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

---Copy a file. The result is similar to the cp shell command.
---
---@param from string path to original file.
---@param to string path to new file.
---@return boolean success, string? error_message
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

---Copy everything in the `from-path`, including subdirectory contents, to
---the `to-path`.
---
---The result is similar to the `cp -r` shell command. The `to-path` should
---not be empty.
---
---@param from string
---@param to string
---@return boolean success, string? error_message
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

---Change the access time and possibly also change the update time of a file.
---
---For details type man 2 utime. Times should be expressed as number of
---seconds since the epoch.
---
---@param path string name.
---@param atime? number time of last access. default current time.
---@param mtime? number time of last update. default = access time.
---@return boolean success, string? error_message
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

---@diagnostic disable-next-line: missing-fields
fio.path = {}
---Check if file or directory is a file.
---
---@param filename string
---@return boolean result `true` if path-name refers to a file; otherwise
---    `false`.
fio.path.is_file = function(filename)
    local fs = fio.stat(filename)
    return fs ~= nil and fs:is_reg() or false
end

---Check if file or directory is a link.
---
---@param filename string
---@return boolean result `true` if path-name refers to a symbolic link;
---    otherwise `false`.
fio.path.is_link = function(filename)
    local fs = fio.lstat(filename)
    return fs ~= nil and fs:is_link() or false
end

---Check if file or directory is a directory.
---
---@param filename string
---@return boolean result `true` if path-name refers to a directory;
---    otherwise `false`.
fio.path.is_dir = function(filename)
    local fs = fio.stat(filename)
    return fs ~= nil and fs:is_dir() or false
end

---Check if file or directory exists.
---
---@param filename string
---@return boolean result `true` if path-name refers to a directory or file
---    that exists and is not a broken symbolic link; otherwise `false`.
fio.path.exists = function(filename)
    return fio.stat(filename) ~= nil
end

---Check if file or directory exists.
---
---@param filename string
---@return boolean result `true` if path-name refers to a directory or file
---    that exists or is a broken symbolic link; otherwise `false`.
fio.path.lexists = function(filename)
    return fio.lstat(filename) ~= nil
end

---@type fio
return fio
