ffi = require('ffi')

--
-- gh-2971: Tarantool should not hide symbols. Even those which
-- are not a part of the public API.
--

-- This symbol is not public, but should be defined.
ffi.cdef[[                                                                      \
bool                                                                            \
box_is_configured(void);                                                        \
]]

ffi.C.box_is_configured()
