local ffi = require("ffi")

-- Checker that takes an array with the following triplets:
-- 1) `sizeof()` for the given C type to be checked.
-- 2) `alignof()` for the given C type to be checked.
-- 3) String representing the C type.
return function(t)
  for i=1,1e9,3 do
    local tp = t[i+2]
    if not tp then break end
    local id = ffi.typeof(tp)
    assert(ffi.sizeof(id) == t[i], tp)
    assert(ffi.alignof(id) == t[i+1], tp)
  end
end
