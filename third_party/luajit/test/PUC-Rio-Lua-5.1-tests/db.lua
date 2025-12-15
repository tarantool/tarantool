-- testing debug library

local function dostring(s) return assert(loadstring(s))() end

print"testing debug library and debug information"

do
local a=1
end

-- The LuaJIT's virtual machine interprets the bytecode
-- following the return from function (i.e. the one succeeding
-- the call made) and located on the line other than that return
-- bytecode, as a new line trigger for line hooks, unlike Lua
-- does.
-- Here is an example (it is joined in one line intend):
--[[
debug.sethook(function(_, l) print("LINE: "..l) end, "l") loadstring("\n\ns=nil")() debug.sethook()
--]]
-- This chunk prints for LuaJIT:
--[[
LINE: 3
LINE: 1
--]]
-- But for Lua 5.1 it is only "LINE: 3" in the output.
-- See also https://github.com/tarantool/tarantool/issues/5693.
-- This function is modified to correspond with LuaJIT's
-- line triggers.
function test (s, l, p)
  collectgarbage()   -- avoid gc during trace
  local function f (event, line)
    assert(event == 'line')
    local l = table.remove(l, 1)
    if p then print(l, line) end
    assert(l == line, "wrong trace!!")
  end
  -- Despite `loadstring` and `debug.sethook` are on the same
  -- line, LuaJIT generates separate line events for them. The
  -- test is adapted to LuaJIT behavior by adding corresponding
  -- line numbers into the test table.
  table.insert(l, 1, 43)
  table.insert(l, 43)
  debug.sethook(f,"l"); loadstring(s)(); debug.sethook()
  assert(table.getn(l) == 0)
end


do
  local a = debug.getinfo(print)
  assert(a.what == "C" and a.short_src == "[C]")
  local b = debug.getinfo(test, "SfL")
  assert(b.name == nil and b.what == "Lua" and b.linedefined == 29 and
         b.lastlinedefined == b.linedefined + 16 and
         b.func == test and not string.find(b.short_src, "%["))
  assert(b.activelines[b.linedefined + 1] and
         b.activelines[b.lastlinedefined])
  assert(not b.activelines[b.linedefined] and
         not b.activelines[b.lastlinedefined + 1])
end


-- test file and string names truncation
a = "function f () end"
local function dostring (s, x) return loadstring(s, x)() end
dostring(a)
assert(debug.getinfo(f).short_src == string.format('[string "%s"]', a))
dostring(a..string.format("; %s\n=1", string.rep('p', 400)))
assert(string.find(debug.getinfo(f).short_src, '^%[string [^\n]*%.%.%."%]$'))
dostring("\n"..a)
assert(debug.getinfo(f).short_src == '[string "..."]')
dostring(a, "")
assert(debug.getinfo(f).short_src == '[string ""]')
dostring(a, "@xuxu")
assert(debug.getinfo(f).short_src == "xuxu")
dostring(a, "@"..string.rep('p', 1000)..'t')
assert(string.find(debug.getinfo(f).short_src, "^%.%.%.p*t$"))
dostring(a, "=xuxu")
assert(debug.getinfo(f).short_src == "xuxu")
dostring(a, string.format("=%s", string.rep('x', 500)))
assert(string.find(debug.getinfo(f).short_src, "^x*"))
dostring(a, "=")
assert(debug.getinfo(f).short_src == "")
a = nil; f = nil;


repeat
  local g = {x = function ()
    local a = debug.getinfo(2)
    assert(a.name == 'f' and a.namewhat == 'local')
    a = debug.getinfo(1)
    assert(a.name == 'x' and a.namewhat == 'field')
    return 'xixi'
  end}
  local f = function () return 1+1 and (not 1 or g.x()) end
  assert(f() == 'xixi')
  g = debug.getinfo(f)
  assert(g.what == "Lua" and g.func == f and g.namewhat == "" and not g.name)

  function f (x, name)   -- local!
    name = name or 'f'
    local a = debug.getinfo(1)
    assert(a.name == name and a.namewhat == 'local')
    return x
  end

  -- breaks in different conditions
  if 3>4 then break end; f()
  if 3<4 then a=1 else break end; f()
  while 1 do local x=10; break end; f()
  local b = 1
  if 3>4 then return math.sin(1) end; f()
  a = 3<4; f()
  a = 3<4 or 1; f()
  repeat local x=20; if 4>3 then f() else break end; f() until 1
  g = {}
  f(g).x = f(2) and f(10)+f(9)
  assert(g.x == f(19))
  function g(x) if not x then return 3 end return (x('a', 'x')) end
  assert(g(f) == 'a')
until 1

test([[if
math.sin(1)
then
  a=1
else
  a=2
end
]], {2,4,7})

test([[--
if nil then
  a=1
else
  a=2
end
]], {2,5,6})

-- Test is adapted to the behaviour of LuaJIT.
test([[a=1
repeat
  a=a+1
until a==3
]], {1,2,3,4,2,3,4})

test([[ do
  return
end
]], {2})

-- Test is adapted to the behaviour of LuaJIT.
test([[local a
a=1
while a<=3 do
  a=a+1
end
]], {1,2,3,4,3,4,3,4,3,5})

test([[while math.sin(1) do
  if math.sin(1)
  then
    break
  end
end
a=1]], {1,2,4,7})

test([[for i=1,3 do
  a=i
end
]], {1,2,1,2,1,2,1,3})

test([[for i,v in pairs{'a','b'} do
  a=i..v
end
]], {1,2,1,2,1,3})

-- Test is adapted to the behaviour of LuaJIT, as it generates
-- only four line events, unlike Lua, which generates five
-- of them.
test([[for i=1,4 do a=1 end]], {1,1,1,1})


print'+'

a = {}; L = nil
local glob = 1
local oldglob = glob
debug.sethook(function (e,l)
  collectgarbage()   -- force GC during a hook
  local f, m, c = debug.gethook()
  assert(m == 'crl' and c == 0)
  if e == "line" then
    if glob ~= oldglob then
      L = l-1   -- get the first line where "glob" has changed
      oldglob = glob
    end
  elseif e == "call" then
      local f = debug.getinfo(2, "f").func
      a[f] = 1
  else assert(e == "return")
  end
end, "crl")

function f(a,b)
  collectgarbage()
  local _, x = debug.getlocal(1, 1)
  local _, y = debug.getlocal(1, 2)
  assert(x == a and y == b)
  assert(debug.setlocal(2, 3, "pera") == "AA".."AA")
  assert(debug.setlocal(2, 4, "maçã") == "B")
  x = debug.getinfo(2)
  assert(x.func == g and x.what == "Lua" and x.name == 'g' and
         x.nups == 0 and string.find(x.source, "^@.*db%.lua"))
  glob = glob+1
  assert(debug.getinfo(1, "l").currentline == L+1)
  assert(debug.getinfo(1, "l").currentline == L+2)
end

function foo()
  glob = glob+1
  assert(debug.getinfo(1, "l").currentline == L+1)
end; foo()  -- set L
-- check line counting inside strings and empty lines

_ = 'alo\
alo' .. [[

]]
--[[
]]
assert(debug.getinfo(1, "l").currentline == L+11)  -- check count of lines


function g(...)
  -- LuaJIT: Lua 5.1 interprets `...` in the vararg functions like
  -- an additional local argument unlike LuaJIT does.
  -- This extension is from Lua 5.2.
  -- See also https://github.com/tarantool/tarantool/issues/5694.
  -- Test is adapted from PUC-Rio Lua 5.2 test suite by adding
  -- additional variable `arg`.
  local arg = {...}
  do local a,b,c; a=math.sin(40); end
  local feijao
  local AAAA,B = "xuxu", "mamão"
  f(AAAA,B)
  assert(AAAA == "pera" and B == "maçã")
  do
     local B = 13
     local x,y = debug.getlocal(1,5)
     assert(x == 'B' and y == 13)
  end
end

g()


assert(a[f] and a[g] and a[assert] and a[debug.getlocal] and not a[print])


-- tests for manipulating non-registered locals (C and Lua temporaries)

local n, v = debug.getlocal(0, 1)
assert(v == 0 and n == "(*temporary)")
local n, v = debug.getlocal(0, 2)
assert(v == 2 and n == "(*temporary)")
assert(not debug.getlocal(0, 3))
assert(not debug.getlocal(0, 0))

function f()
  assert(select(2, debug.getlocal(2,3)) == 1)
  assert(not debug.getlocal(2,4))
  debug.setlocal(2, 3, 10)
  return 20
end

function g(a,b) return (a+1) + f() end

assert(g(0,0) == 30)


debug.sethook(nil);
assert(debug.gethook() == nil)


-- testing access to function arguments

X = nil
a = {}
function a:f (a, b, ...) local c = 13 end
debug.sethook(function (e)
  assert(e == "call")
  dostring("XX = 12")  -- test dostring inside hooks
  -- testing errors inside hooks
  assert(not pcall(loadstring("a='joao'+1")))
  debug.sethook(function (e, l)
    assert(debug.getinfo(2, "l").currentline == l)
    local f,m,c = debug.gethook()
    assert(e == "line")
    assert(m == 'l' and c == 0)
    debug.sethook(nil)  -- hook is called only once
    assert(not X)       -- check that
    X = {}; local i = 1
    local x,y
    while 1 do
      x,y = debug.getlocal(2, i)
      if x==nil then break end
      X[x] = y
      i = i+1
    end
  end, "l")
end, "c")

a:f(1,2,3,4,5)

-- LuaJIT: Lua 5.1 interprets `...` in the vararg functions as
-- an additional local argument unlike LuaJIT does.
-- So, `a:f()` function will not set the corresponding `arg`
-- table, as test expects.
-- Implicit `arg` parameter for old-style vararg functions was
-- finally removed in Lua 5.2
-- The test is adapted from PUC-Rio Lua 5.2 test suite by removing
-- additional `arg.n == 3` check.
assert(X.self == a and X.a == 1   and X.b == 2 and X.c == nil)
assert(XX == 12)
assert(debug.gethook() == nil)


-- testing upvalue access
local function getupvalues (f)
  local t = {}
  local i = 1
  while true do
    local name, value = debug.getupvalue(f, i)
    if not name then break end
    assert(not t[name])
    t[name] = value
    i = i + 1
  end
  return t
end

local a,b,c = 1,2,3
local function foo1 (a) b = a; return c end
local function foo2 (x) a = x; return c+b end
assert(debug.getupvalue(foo1, 3) == nil)
assert(debug.getupvalue(foo1, 0) == nil)
assert(debug.setupvalue(foo1, 3, "xuxu") == nil)
local t = getupvalues(foo1)
assert(t.a == nil and t.b == 2 and t.c == 3)
t = getupvalues(foo2)
assert(t.a == 1 and t.b == 2 and t.c == 3)
assert(debug.setupvalue(foo1, 1, "xuxu") == "b")
assert(({debug.getupvalue(foo2, 3)})[2] == "xuxu")
-- cannot manipulate C upvalues from Lua
assert(debug.getupvalue(io.read, 1) == nil)
assert(debug.setupvalue(io.read, 1, 10) == nil)


-- testing count hooks
local a=0
-- LuaJIT: LuaJIT does not check hooks at traces without defined
-- -DLUAJIT_ENABLE_CHECKHOOK.
-- For more information see <src/lj_record.c> or commit
-- 6bce6b118eeb2bb7f36157de158e5cccf0ea68e5 (Add compile-time
-- option LUAJIT_ENABLE_CHECKHOOK. Disabled by default.).
-- See also https://github.com/tarantool/tarantool/issues/5701
-- Test is adapted for LuaJIT by disabling JIT while
-- testing count hooks.
local jit_is_enabled = jit.status()
if jit_is_enabled then
  jit.off()
end
debug.sethook(function (e) a=a+1 end, "", 1)
a=0; for i=1,1000 do end; assert(1000 < a and a < 1012)
debug.sethook(function (e) a=a+1 end, "", 4)
a=0; for i=1,1000 do end; assert(250 < a and a < 255)
local f,m,c = debug.gethook()
assert(m == "" and c == 4)
debug.sethook(function (e) a=a+1 end, "", 4000)
a=0; for i=1,1000 do end; assert(a == 0)
debug.sethook(print, "", 2^24 - 1)   -- count upperbound
local f,m,c = debug.gethook()
assert(({debug.gethook()})[3] == 2^24 - 1)
debug.sethook()
if jit_is_enabled then
  jit.on()
end


-- tests for tail calls
-- LuaJIT does not provide information about tail calls,
-- unlike Lua does. See also https://luajit.org/status.html.
-- getfenv() behavior is also different here, because tail calls
-- do not provide additional call frame for LuaJIT.
-- See also https://github.com/tarantool/tarantool/issues/5702.
-- This function is adapted to LuaJIT behavior.
local function f (x)
  if x then
    assert(debug.getinfo(1, "S").what == "Lua")
    local tail = debug.getinfo(2)
    assert(tail.what == "Lua" and tail.linedefined == 410 and tail.func == g1)
    assert(getfenv(3))
    assert(debug.getinfo(3, "S").what == "main")
    print"+"
    end
end

function g(x) return f(x) end

function g1(x) g(x) end

local function h (x) local f=g1; return f(x) end

h(true)

local b = {}
debug.sethook(function (e) table.insert(b, e) end, "cr")
-- This function is adapted to LuaJIT behavior.
-- See the comment above.
h(false)
debug.sethook()
-- This chunk is adapted to LuaJIT behavior.
-- See the comment above.
local res = {
  -- XXX: there is no "return" for debug.sethook() since it is
  -- LuaJIT builtin function and its return is not dispatched via
  -- RETM, RET, RET0 or RET1.
  "call", "call", "call", "call",
  "return", "return",
  "call",    -- last call (to sethook)
}
for _, k in ipairs(res) do assert(k == table.remove(b, 1)) end


lim = 30000
-- This function is adapted to LuaJIT behavior.
-- See the comment above.
local function foo (x)
  if x==0 then
    -- XXX: As it is mentioned above, there is no additional
    -- frames created for tail call, so only two frames need to
    -- be checked: the one used for foo(0) (i.e debug.getinfo()
    -- caller) and the one used for foo(lim).
    assert(debug.getinfo(2, "S").what == "main")
    assert(debug.getinfo(3, "S").what == "main")
  else return foo(x-1)
  end
end

foo(lim)


print"+"


-- testing traceback

assert(debug.traceback(print) == print)
assert(debug.traceback(print, 4) == print)
assert(string.find(debug.traceback("hi", 4), "^hi\n"))
assert(string.find(debug.traceback("hi"), "^hi\n"))
assert(not string.find(debug.traceback("hi"), "'traceback'"))
assert(string.find(debug.traceback("hi", 0), "'traceback'"))
assert(string.find(debug.traceback(), "^stack traceback:\n"))

-- testing debugging of coroutines

local function checktraceback (co, p)
  local tb = debug.traceback(co)
  local i = 0
  for l in string.gmatch(tb, "[^\n]+\n?") do
    assert(i == 0 or string.find(l, p[i]))
    i = i+1
  end
  assert(p[i] == nil)
end


local function f (n)
  if n > 0 then return f(n-1)
  else coroutine.yield() end
end

local co = coroutine.create(f)
coroutine.resume(co, 3)
-- Test is adapted to the behavior of LuaJIT.
checktraceback(co, {"yield", "db.lua"})


co = coroutine.create(function (x)
       local a = 1
       coroutine.yield(debug.getinfo(1, "l"))
       coroutine.yield(debug.getinfo(1, "l").currentline)
       return a
     end)

local tr = {}
local foo = function (e, l) table.insert(tr, l) end
debug.sethook(co, foo, "l")

local _, l = coroutine.resume(co, 10)
local x = debug.getinfo(co, 1, "lfLS")
assert(x.currentline == l.currentline and x.activelines[x.currentline])
assert(type(x.func) == "function")
-- LuaJIT: LuaJIT does not report the line with single "end"
-- statement (the last line of the function) as an active line in
-- debug.getinfo(), unlike Lua does. There is no bytecode
-- related to this line, so it is "unreachable" and
-- may be considered not active.
-- See also https://github.com/tarantool/tarantool/issues/5708.
-- Test is adapted for LuaJIT's behaviour by avoiding the last
-- line check.
for i=x.linedefined + 1, x.lastlinedefined - 1 do
  assert(x.activelines[i])
  x.activelines[i] = nil
end
assert(next(x.activelines) == nil)   -- no 'extra' elements
assert(debug.getinfo(co, 2) == nil)
local a,b = debug.getlocal(co, 1, 1)
assert(a == "x" and b == 10)
a,b = debug.getlocal(co, 1, 2)
assert(a == "a" and b == 1)
debug.setlocal(co, 1, 2, "hi")
assert(debug.gethook(co) == foo)
-- LuaJIT: LuaJIT does not support per-coroutine hooks.
-- Hook is set for the whole VM.
-- See also https://luajit.org/status.html.
-- Test is disabled for LuaJIT.
-- assert(table.getn(tr) == 2 and
--        tr[1] == l.currentline-1 and tr[2] == l.currentline)

a,b,c = pcall(coroutine.resume, co)
assert(a and b and c == l.currentline+1)
checktraceback(co, {"yield", "in function <"})

a,b = coroutine.resume(co)
assert(a and b == "hi")
-- LuaJIT: Behavior is different for LuaJIT.
-- See the comment above. Test is disabled for LuaJIT.
-- assert(table.getn(tr) == 4 and tr[4] == l.currentline+2)
assert(debug.gethook(co) == foo)
-- LuaJIT: Behavior is different for LuaJIT.
-- See the comment above. Test is disabled for LuaJIT.
-- assert(debug.gethook() == nil)
checktraceback(co, {})


-- check traceback of suspended (or dead with error) coroutines

function f(i) if i==0 then error(i) else coroutine.yield(); f(i-1) end end

co = coroutine.create(function (x) f(x) end)
a, b = coroutine.resume(co, 3)
t = {"'yield'", "'f'", "in function <"}
while coroutine.status(co) == "suspended" do
  checktraceback(co, t)
  a, b = coroutine.resume(co)
  table.insert(t, 2, "'f'")   -- one more recursive call to 'f'
end
t[1] = "'error'"
checktraceback(co, t)


-- test accessing line numbers of a coroutine from a resume inside
-- a C function (this is a known bug in Lua 5.0)

local function g(x)
    coroutine.yield(x)
end

local function f (i)
  debug.sethook(function () end, "l")
  for j=1,1000 do
    g(i+j)
  end
end

local co = coroutine.wrap(f)
co(10)
pcall(co)
pcall(co)


assert(type(debug.getregistry()) == "table")


print"OK"

