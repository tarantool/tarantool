env = require('test_run')
test_run = env.new()

-- gh-1094: box.snapshot() aborts if the server is out of file descriptors
ffi=require('ffi')

test_run:cmd("setopt delimiter ';;;'")

ffi.cdef[[
struct rlimit {unsigned long cur; unsigned long max;};
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);

]];;;

test_run:cmd("setopt delimiter ''");;;

_ = box.space._schema:insert({'gh1094'})

r = ffi.new('struct rlimit')

ffi.C.getrlimit(7, r)
orig_limit = r.cur;
r.cur = 1
ffi.C.setrlimit(7, r)
box.snapshot()
r.cur = orig_limit
ffi.C.setrlimit(7, r)

box.snapshot()

_ = box.space._schema:delete({'gh1094'})
