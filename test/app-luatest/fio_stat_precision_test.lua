local t = require('luatest')
local fio = require('fio')
local ffi = require('ffi')

ffi.cdef[[
    typedef long time_t;
    struct timespec {
        time_t tv_sec;
        long tv_nsec;
    };
    int futimens(int fd, const struct timespec times[2]);
]]

local seconds = 1700000000
local atime_nsec = 125000000
local mtime_nsec = 875000000
local atime = seconds + atime_nsec / 1e9
local mtime = seconds + mtime_nsec / 1e9

local g = t.group('fio_stat_precision')

g.before_all(function(cg)
    cg.dir = fio.tempdir()
    cg.path = fio.pathjoin(cg.dir, 'file')
    cg.fh = assert(fio.open(cg.path, {'O_CREAT', 'O_RDWR'}, 384))
    local times = ffi.new('struct timespec[2]', {
        {seconds, atime_nsec},
        {seconds, mtime_nsec},
    })
    t.assert_equals(ffi.C.futimens(cg.fh.fh, times), 0)
end)

g.after_all(function(cg)
    if cg.fh ~= nil then
        cg.fh:close()
    end
    if cg.dir ~= nil then
        fio.rmtree(cg.dir)
    end
end)

local function assert_stat_precision(st)
    t.assert(st)
    t.assert_equals(st.atime, atime)
    t.assert_equals(st.mtime, mtime)
end

g.test_stat_precision = function(cg)
    assert_stat_precision(fio.stat(cg.path))
end

g.test_lstat_precision = function(cg)
    assert_stat_precision(fio.lstat(cg.path))
end

g.test_fstat_precision = function(cg)
    assert_stat_precision(cg.fh:stat())
end
