
local ffi = require('ffi')
pcall(ffi.cdef, [[
typedef long rlim_t;
struct rlimit {
    rlim_t rlim_cur;  /* Soft limit */
    rlim_t rlim_max;  /* Hard limit (ceiling for rlim_cur) */
};
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);
]])

return {
    RLIMIT_NOFILE = jit.os == 'OSX' and 8 or 7,
    limit = function()
        return ffi.new('struct rlimit')
    end,
    getrlimit = function (id, limit)
        ffi.C.getrlimit(id, limit)
    end,
    setrlimit = function (id, limit)
        ffi.C.setrlimit(id, limit)
    end,
}
