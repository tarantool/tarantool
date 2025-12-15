local ffi = require('ffi')

-- Overloaded `dlerror()` returns NULL after trying to load an
-- unexisting file.
local res, errmsg = pcall(ffi.load, 'lj-522-fix-dlerror-return-null-unexisted')

assert(not res, 'pcall should fail')

-- Return the error message to be checked by the TAP.
io.write(errmsg)
