local ffi = require("ffi")

-- Checker that takes an array of strings that should represent
-- different invalid CTypes (a more common pattern). Also, the
-- second argument may be also the `loadstring` function to check
-- invalid literals or `ffi.cdef` to check invalid C definitions.
return function(t, f)
  f = f or ffi.typeof
  for i=1,1e9 do
    local tp = t[i]
    if not tp then break end
    assert(pcall(f, tp) == false, tp)
  end
end
