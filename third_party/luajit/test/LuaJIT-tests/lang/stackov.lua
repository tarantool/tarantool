local function f()
  f()
end
jit.off(f)

local function g(i)
  g(i)
end
jit.off(g)

local function vtail(...)
  return vtail(1, ...)
end
jit.off(vtail)

local function vcall(...)
  vcall(1, ...)
end
jit.off(vcall)

local function test_error_msg(func, s)
  local first = string.match(s, "[^\n]+")
  local line = debug.getinfo(func, "S").linedefined + 1
  assert(string.match(first, ":" .. line .. ": stack overflow$"))

  local n = 1
  for _ in string.gmatch(s, "\n") do n = n + 1 end
  assert(n == 1 + 1 + 11 + 1 + 10)
end

do --- Base test.
  local err, s = xpcall(f, debug.traceback)
  assert(err == false)
  -- There is no place on the stack to invoke the handler.
  -- Just test the error reason.
  assert(string.match(s, "stack overflow"))
end

do --- Stack overflow with non-empty arg list.
  local err, s = xpcall(g, debug.traceback, 1)
  assert(err == false)
  -- There is no place on the stack to invoke the handler.
  -- Just test the error reason.
  assert(string.match(s, "stack overflow"))
end

do --- Vararg tail call with non-empty arg list. +slow
  local err, s = xpcall(vtail, debug.traceback, 1)
  assert(err == false)
end

do --- Vararg non-tail call.
  local err, s = xpcall(vcall, debug.traceback, 1)
  assert(err == false)
  test_error_msg(vcall, s)
end
