local ffi = require('ffi')
local errno_list = require('errno.lib')

ffi.cdef[[
    const char *tt_strerror(int errnum);
]]

---The errno module is typically used within a function or within a Lua
---program, in association with a module whose functions can return
---operating-system errors, such as [fio](lua://fio).
---
---**Example:**
---
---```tarantoolsession
---tarantool> function f()
---         >   local fio = require('fio')
---         >   local errno = require('errno')
---         >   fio.open('no_such_file')
---         >   print('errno() = ' .. errno())
---         >   print('errno.strerror() = ' .. errno.strerror())
---         >   local t = getmetatable(errno).__index
---         >   for k, v in pairs(t) do
---         >     if v == errno() then
---         >       print('errno() constant = ' .. k)
---         >     end
---         >   end
---         > end
--- ---
--- ...
---
---tarantool> f()
---errno() = 2
---errno.strerror() = No such file or directory
---errno() constant = ENOENT
--- ---
--- ...
---```
---
---To see all possible error names stored in the `errno` metatable, say
---`getmetatable(errno)` (output abridged):
---
---```tarantoolsession
---tarantool> getmetatable(errno)
---    ---
---    - __newindex: 'function: 0x41666a38'
---      __call: 'function: 0x41666890'
---      __index:
---      ENOLINK: 67
---      EMSGSIZE: 90
---      EOVERFLOW: 75
---      ENOTCONN: 107
---      EFAULT: 14
---      EOPNOTSUPP: 95
---      EEXIST: 17
---      ENOSR: 63
---      ENOTSOCK: 88
---      EDESTADDRREQ: 89
---      <...>
---    ...
---```
---@class errno: table
---@field E2BIG integer Argument list too long (POSIX.1-2001).
---@field EACCES integer Permission denied (POSIX.1-2001).
---@field EADDRINUSE integer Address already in use (POSIX.1-2001).
---@field EADDRNOTAVAIL integer Address not available (POSIX.1-2001).
---@field EAFNOSUPPORT integer Address family not supported (POSIX.1-2001).
---@field EAGAIN integer Resource temporarily unavailable (may be the
---    same value as EWOULDBLOCK) (POSIX.1-2001).
---@field EALREADY integer Connection already in progress (POSIX.1-2001).
---@field EBADF integer Bad file descriptor (POSIX.1-2001).
---@field EBADMSG integer Bad message (POSIX.1-2001).
---@field EBUSY integer Device or resource busy (POSIX.1-2001).
---@field ECANCELED integer Operation canceled (POSIX.1-2001).
---@field ECHILD integer No child processes (POSIX.1-2001).
---@field ECONNABORTED integer Connection aborted (POSIX.1-2001).
---@field ECONNREFUSED integer Connection refused (POSIX.1-2001).
---@field ECONNRESET integer Connection reset (POSIX.1-2001).
---@field EDEADLK integer Resource deadlock avoided (POSIX.1-2001).
---@field EDESTADDRREQ integer Destination address required (POSIX.1-2001).
---@field EDOM integer Mathematics argument out of domain of function
---    (POSIX.1, C99).
---@field EDQUOT integer Disk quota exceeded (POSIX.1-2001).
---@field EEXIST integer File exists (POSIX.1-2001).
---@field EFAULT integer Bad address (POSIX.1-2001).
---@field EFBIG integer File too large (POSIX.1-2001).
---@field EHOSTUNREACH integer Host is unreachable (POSIX.1-2001).
---@field EIDRM integer Identifier removed (POSIX.1-2001).
---@field EILSEQ integer Invalid or incomplete multibyte or wide
---    character (POSIX.1, C99).
---@field EINPROGRESS integer Operation in progress (POSIX.1-2001).
---@field EINTR integer Interrupted function call (POSIX.1-2001); see signal(7).
---@field EINVAL integer Invalid argument (POSIX.1-2001).
---@field EIO integer Input/output error (POSIX.1-2001).
---@field EISCONN integer Socket is connected (POSIX.1-2001).
---@field EISDIR integer Is a directory (POSIX.1-2001).
---@field ELOOP integer Too many levels of symbolic links (POSIX.1-2001).
---@field EMFILE integer Too many open files (POSIX.1-2001). Commonly caused
---    by exceeding the RLIMIT_NOFILE resource limit described in
---    getrlimit(2). Can also be caused by exceeding the limit specified in
---    /proc/sys/fs/nr_open.
---@field EMLINK integer Too many links (POSIX.1-2001).
---@field EMSGSIZE integer Message too long (POSIX.1-2001).
---@field EMULTIHOP integer Multihop attempted (POSIX.1-2001).
---@field ENAMETOOLONG integer Filename too long (POSIX.1-2001).
---@field ENETDOWN integer Network is down (POSIX.1-2001).
---@field ENETRESET integer Connection aborted by network (POSIX.1-2001).
---@field ENETUNREACH integer Network unreachable (POSIX.1-2001).
---@field ENFILE integer Too many open files in system (POSIX.1-2001). On
---    Linux, this is probably a result of encountering the
---    /proc/sys/fs/file-max limit (see proc(5)).
---@field ENOBUFS integer No buffer space available (POSIX.1 (XSI STREAMS
---    option)).
---@field ENODATA integer The named attribute does not exist, or the process
---    has no access to this attribute; see xattr(7).
---@field ENODEV integer No such device (POSIX.1-2001).
---@field ENOENT integer No such file or directory (POSIX.1-2001).
---@field ENOEXEC integer Exec format error (POSIX.1-2001).
---@field ENOLCK integer No locks available (POSIX.1-2001).
---@field ENOLINK integer Link has been severed (POSIX.1-2001).
---@field ENOMEM integer Not enough space/cannot allocate memory (POSIX.1-2001).
---@field ENOMSG integer No message of the desired type (POSIX.1-2001).
---@field ENOPROTOOPT integer Protocol not available (POSIX.1-2001).
---@field ENOSPC integer No space left on device (POSIX.1-2001).
---@field ENOSR integer No STREAM resources (POSIX.1 (XSI STREAMS option)).
---@field ENOSTR integer Not a STREAM (POSIX.1 (XSI STREAMS option)).
---@field ENOSYS integer Function not implemented (POSIX.1-2001).
---@field ENOTCONN integer The socket is not connected (POSIX.1-2001).
---@field ENOTDIR integer Not a directory (POSIX.1-2001).
---@field ENOTEMPTY integer Directory not empty (POSIX.1-2001).
---@field ENOTSOCK integer Not a socket (POSIX.1-2001).
---@field ENOTSUP integer Operation not supported (POSIX.1-2001).
---@field ENOTTY integer Inappropriate I/O control operation (POSIX.1-2001).
---@field ENXIO integer No such device or address (POSIX.1-2001).
---@field EOPNOTSUPP integer Operation not supported on socket (POSIX.1-2001).
---@field EOVERFLOW integer Value too large to be stored in data type
---    (POSIX.1-2001).
---@field EPERM integer Operation not permitted (POSIX.1-2001).
---@field EPIPE integer Broken pipe (POSIX.1-2001).
---@field EPROTO integer Protocol error (POSIX.1-2001).
---@field EPROTONOSUPPORT integer Protocol not supported (POSIX.1-2001).
---@field EPROTOTYPE integer Protocol wrong type for socket (POSIX.1-2001).
---@field ERANGE integer Result too large (POSIX.1, C99).
---@field EROFS integer Read-only filesystem (POSIX.1-2001).
---@field ESPIPE integer Invalid seek (POSIX.1-2001).
---@field ESRCH integer No such process (POSIX.1-2001).
---@field ESTALE integer Stale file handle (POSIX.1-2001).
---@field ETIME integer Timer expired (POSIX.1 (XSI STREAMS option)).
---@field ETIMEDOUT integer Connection timed out (POSIX.1-2001).
---@field ETXTBSY integer Text file busy (POSIX.1-2001).
---@field EWOULDBLOCK integer Operation would block (may be same value as
---    EAGAIN) (POSIX.1-2001).
---@field EXDEV integer Improper link (POSIX.1-2001).
---Return an error number for the last operating-system-related function,
---or 0. If `errno` is supplied, set it and return the previous value.
---@overload fun(errno?: integer): integer
local errno = {}

---Return a string, given an error number.
---
---The string will contain the text of the conventional error message for
---the current operating system. If `errno` is not supplied, the error
---message will be for the last operating-system-related function, or 0.
---
---@param errno? integer number of an operating-system error.
---@return string message the error message string.
function errno.strerror(errno)
    if errno == nil then
        errno = ffi.errno()
    end
    return ffi.string(ffi.C.tt_strerror(tonumber(errno)))
end

setmetatable(errno, {
    __index = errno_list,
    __newindex = function() error("Can't create new errno constants") end,
    __call = function(self, ...) return ffi.errno(...) end
})

return errno
