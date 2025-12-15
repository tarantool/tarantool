--- tap.lua internal file.
---
--- The Test Anything Protocol version 13 producer.
---

-- Initializer FFI for <iscdata> check.
local ffi = require("ffi")
local NULL = ffi.new("void *")

local function finalize(test)
  -- TODO: implement finalization for subtests too.
  assert(test.parent == nil, 'FIXME: There is no way to use finalize subtest')
  test:done(true)
end

local function indent(level, size)
  -- Use the default indent size if none is specified.
  size = tonumber(size) or 4
  return (" "):rep(level * size)
end

local function traceback(level)
  local trace = {}
  -- The default level is 3 since at this point you have the
  -- following frame layout
  -- frame #0: <debug.getinfo> (its call is below)
  -- frame #1: <traceback> (this function)
  -- frame #2: <ok> (the "dominator" function for all assertions)
  -- XXX: all exported assertions call <ok> function using tail
  -- call (i.e. "return ok(...)"), so frame #3 contains the
  -- assertion function used in the test chunk.
  level = level or 3
  while true do
    local info = debug.getinfo(level, "nSl")
    if not info then
      break
    end
    table.insert(trace, {
      source   = info.source,
      src      = info.short_src,
      line     = info.linedefined or 0,
      what     = info.what,
      name     = info.name,
      namewhat = info.namewhat,
      filename = info.source:sub(1, 1) == "@" and info.source:sub(2) or "eval",
    })
    level = level + 1
  end
  return trace
end

local function diag(test, fmt, ...)
  io.write(indent(test.level), ("# %s\n"):format(fmt:format(...)))
end

local function ok(test, cond, message, extra)
  test.total = test.total + 1
  if cond then
    io.write(indent(test.level), ("ok - %s\n"):format(message))
    return true
  end

  test.failed = test.failed + 1
  io.write(indent(test.level), ("not ok - %s\n"):format(message))

  -- Dump extra contents added in outer space.
  for key, value in pairs(extra or {}) do
    -- XXX: Use "half indent" to dump <extra> fields.
    io.write(indent(test.level + 0.5), ("%s:\t%s\n"):format(key, value))
  end

  if not test.trace then
    return false
  end

  local trace = traceback()
  local tindent = indent(test.level + 1)
  io.write(tindent, ("filename:\t%s\n"):format(trace[#trace].filename))
  io.write(tindent, ("line:\t%s\n"):format(trace[#trace].line))
  for frameno, frame in ipairs(trace) do
    io.write(tindent, ("frame #%d\n"):format(frameno))
    -- XXX: Use "half indent" to dump <frame> fields.
    local findent = indent(0.5) .. tindent
    for key, value in pairs(frame) do
      io.write(findent, ("%s:\t%s\n"):format(key, value))
    end
  end
  return false
end

local function fail(test, message, extra)
  return ok(test, false, message, extra)
end

local function skip(test, message, extra)
  ok(test, true, message .. " # skip", extra)
end

local function skipall(test, reason)
  test:plan(0, reason)
  finalize(test)
end

local function skiprest(test, reason)
  for _ = 1, test.planned - test.total do
    test:skip(reason)
  end
  finalize(test)
end

local function like(test, got, pattern, message, extra)
  extra = extra or {}
  extra.got = got
  extra.expected = pattern
  return ok(test, tostring(got):match(pattern) ~= nil, message, extra)
end

local function unlike(test, got, pattern, message, extra)
  extra = extra or {}
  extra.got = got
  extra.expected = pattern
  return ok(test, tostring(got):match(pattern) == nil, message, extra)
end

local function is(test, got, expected, message, extra)
  extra = extra or {}
  extra.got = got
  extra.expected = expected
  local rc = (test.strict == false or type(got) == type(expected))
             and got == expected
  return ok(test, rc, message, extra)
end

local function isnt(test, got, unexpected, message, extra)
  extra = extra or {}
  extra.got = got
  extra.unexpected = unexpected
  local rc = (test.strict == true and type(got) ~= type(unexpected))
             or got ~= unexpected
  return ok(test, rc, message, extra)
end

local function is_deeply(test, got, expected, message, extra)
  extra = extra or {}
  extra.got = got
  extra.expected = expected
  extra.strict = test.strict

  local seen = {}
  local function cmpdeeply(got, expected, extra) --luacheck: ignore
    if type(expected) == "number" or type(got) == "number" then
      extra.got = got
      extra.expected = expected
      -- Handle NaN.
      if got ~= got and expected ~= expected then
        return true
      end
      return got == expected
    end

    if ffi.istype("bool", got) then got = (got == 1) end
    if ffi.istype("bool", expected) then expected = (expected == 1) end

    if extra.strict and type(got) ~= type(expected) then
      extra.got = type(got)
      extra.expected = type(expected)
      return false
    end

    if type(got) ~= "table" or type(expected) ~= "table" then
      extra.got = got
      extra.expected = expected
      return got == expected
    end

    -- Stop if tables are equal or <got> has been already seen.
    if got == expected or seen[got] then
      return true
    end
    seen[got] = true

    local path = extra.path or "obj"
    local has = {}

    for k, v in pairs(got) do
      has[k] = true
      extra.path = path .. "." .. tostring(k)
      if not cmpdeeply(v, expected[k], extra) then
        return false
      end
    end

    -- Check if expected contains more keys then got.
    for k, v in pairs(expected) do
      if has[k] ~= true and (extra.strict or v ~= NULL) then
        extra.path = path .. "." .. tostring(k)
        extra.expected = "key (exists): " ..  tostring(k)
        extra.got = "key (missing): " .. tostring(k)
        return false
      end
    end

    extra.path = path

    return true
  end

  return ok(test, cmpdeeply(got, expected, extra), message, extra)
end

local function isnil(test, v, message, extra)
  return is(test, type(v), "nil", message, extra)
end

local function isnumber(test, v, message, extra)
  return is(test, type(v), "number", message, extra)
end

local function isstring(test, v, message, extra)
  return is(test, type(v), "string", message, extra)
end

local function istable(test, v, message, extra)
  return is(test, type(v), "table", message, extra)
end

local function isboolean(test, v, message, extra)
  return is(test, type(v), "boolean", message, extra)
end

local function isfunction(test, v, message, extra)
  return is(test, type(v), "function", message, extra)
end

local function isudata(test, v, utype, message, extra)
  extra = extra or {}
  extra.expected = ("userdata<%s>"):format(utype)
  if type(v) ~= "userdata" then
    extra.got = type(v)
    return fail(test, message, extra)
  end
  extra.got = ("userdata<%s>"):format(getmetatable(v))
  return ok(test, getmetatable(v) == utype, message, extra)
end

local function iscdata(test, v, ctype, message, extra)
  extra = extra or {}
  extra.expected = ffi.typeof(ctype)
  if type(v) ~= "cdata" then
    extra.got = type(v)
    return fail(test, message, extra)
  end
  extra.got = ffi.typeof(v)
  return ok(test, ffi.istype(ctype, v), message, extra)
end

local function isnan(v)
  return v ~= v
end

local function samevalues(test, got, message, extra)
  for i = 1, table.maxn(got) - 1 do
    if got[i] ~= got[i + 1] and not (isnan(got[i]) and isnan(got[i + 1])) then
      return fail(test, message, extra)
    end
  end
  return ok(test, true, message, extra)
end

local test_mt

local function new(parent, name, fun, ...)
  local level = parent ~= nil and parent.level + 1 or 0
  local test = setmetatable({
    parent  = parent,
    name    = name,
    level   = level,
    total   = 0,
    failed  = 0,
    planned = nil,
    trace   = parent == nil and true or parent.trace,
    -- Set test.strict = true if test:is, test:isnt and
    -- test:is_deeply must be compared strictly with nil.
    -- Set test.strict = false if nil and box.NULL both have the
    -- same effect. The default is false. For example, if and only
    -- if test.strict = true has happened, then the following
    -- assertion will return false.
    -- test:is_deeply({a = box.NULL}, {})
    strict  = false,
  }, test_mt)
  if fun == nil then
    return test
  end
  test:diag("%s", test.name)
  fun(test, ...)
  test:diag("%s: end", test.name)
  return test:check()
end

local function plan(test, planned, reason)
  if test.planned then
    -- Use <plan> call location in the error message.
    error("plan called twice", 2)
  end
  test.planned = planned
  local tail = planned == 0 and (" # SKIP %s"):format(reason) or ""
  io.write(indent(test.level), ("1..%d%s\n"):format(planned, tail))
end

local function check(test)
  if test.checked then
    -- Use <check> call location in the error message.
    error("check called twice", 2)
  end
  test.checked = true
  if test.planned ~= test.total then
    if test.parent ~= nil then
      ok(test.parent, false, "bad plan", {
        planned = test.planned,
        run = test.total,
      })
    else
      diag(test, ("bad plan: planned %d run %d")
        :format(test.planned, test.total))
    end
  elseif test.failed > 0 then
    if test.parent ~= nil then
      ok(test.parent, false, "failed subtests", {
        failed = test.failed,
        planned = test.planned,
      })
    else
      diag(test, "failed subtest: %d", test.failed)
    end
  else
    if test.parent ~= nil then
      ok(test.parent, true, test.name)
    end
  end
  return test.planned == test.total and test.failed == 0
end

local function skipcond(test, conditions)
  for reason, condition in pairs(conditions) do
    if condition then
      local skipfunc = test.planned and skiprest or skipall
      skipfunc(test, reason)
    end
  end
  return test
end

local function done(test, exit)
  if exit then
    -- XXX: The second argument is required to properly close Lua
    -- universe (i.e. invoke <lua_close> before exiting).
    -- XXX: return is added explicitly to force CALLT emitting.
    return os.exit(test:check() and 0 or 1, true)
  else
    assert(test:check(), 'Test check failed')
  end
end

test_mt = {
  __index = {
    test       = new,
    plan       = plan,
    check      = check,
    diag       = diag,
    done       = done,
    ok         = ok,
    fail       = fail,
    skip       = skip,
    skipall    = skipall,
    skiprest   = skiprest,
    skipcond   = skipcond,
    is         = is,
    isnt       = isnt,
    isnil      = isnil,
    isnumber   = isnumber,
    isstring   = isstring,
    istable    = istable,
    isboolean  = isboolean,
    isfunction = isfunction,
    isudata    = isudata,
    iscdata    = iscdata,
    is_deeply  = is_deeply,
    samevalues = samevalues,
    like       = like,
    unlike     = unlike,
  }
}

return {
  test = function(...)
    io.write("TAP version 13\n")
    return new(nil, ...)
  end
}
