local cp = require("libcpptest")

local unwind

do --- catch, no error
  cp.wrapon()
  local a, b = pcall(cp.catch, function() return "x" end)
  assert(a == true and b == "x")
  cp.wrapoff()
end

do --- pcall throw
  cp.wrapon()
  local a, b = pcall(function() cp.throw("foo") end)
  assert(a == false and b == "foo")
  cp.wrapoff()
end

do --- catch throw
  cp.wrapon()
  local a, b = pcall(cp.catch, function() cp.throw("foo") end)
  unwind = a
  assert((a == false and b == "foo") or (a == true and b == "catch ..."))
  cp.wrapoff()
end

do --- alloc, no error
  cp.wrapon()
  local st = cp.alloc(function() return cp.isalloc() end)
  assert(st == true)
  assert(cp.isalloc() == false)
  cp.wrapoff()
end

do --- throw in alloc
  cp.wrapon()
  local a, b = pcall(cp.alloc, function()
    assert(cp.isalloc() == true)
    return "foo", cp.throw
  end)
  assert(a == false and b == "foo")
  assert(cp.isalloc() == false)
  cp.wrapoff()
end

do --- error in alloc
  cp.wrapon()
  local a, b = pcall(cp.alloc, function()
    assert(cp.isalloc() == true)
    return "foo", error
  end)
  assert(a == false and b == "foo")
  if unwind then assert(cp.isalloc() == false) end
  cp.wrapoff()
end

