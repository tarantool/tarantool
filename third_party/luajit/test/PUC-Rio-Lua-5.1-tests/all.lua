#!../lua

math.randomseed(0)

collectgarbage("setstepmul", 180)
collectgarbage("setpause", 190)


--[=[
  example of a long [comment],
  [[spanning several [lines]]]

]=]

print("current path:\n  " .. string.gsub(package.path, ";", "\n  "))


local msgs = {}
function Message (m)
  print(m)
  msgs[#msgs+1] = string.sub(m, 3, -3)
end


local c = os.clock()

assert(os.setlocale"C")

local T,print,gcinfo,format,write,assert,type =
      T,print,gcinfo,string.format,io.write,assert,type

local function formatmem (m)
  if m < 1024 then return m
  else
    m = m/1024 - m/1024%1
    if m < 1024 then return m.."K"
    else
      m = m/1024 - m/1024%1
      return m.."M"
    end
  end
end

local showmem = function ()
  if not T then
    print(format("    ---- total memory: %s ----\n", formatmem(gcinfo())))
  else
    T.checkmemory()
    local a,b,c = T.totalmem()
    local d,e = gcinfo()
    print(format(
  "\n    ---- total memory: %s (%dK), max use: %s,  blocks: %d\n",
                        formatmem(a),  d,      formatmem(c),           b))
  end
end


--
-- redefine dofile to run files through dump/undump
--
dofile = function (n)
  showmem()
  local f = assert(loadfile(n))
  local b = string.dump(f)
  f = assert(loadstring(b))
  return f()
end

-- LuaJIT: Adapt tests for testing with out-of-source build.
-- See the comment in <test/luajit-test-init.lua>.
local _dofile = _dofile or dofile
local _loadstring = _loadstring or loadstring

_dofile('main.lua')

do
  local u = newproxy(true)
  local newproxy, stderr = newproxy, io.stderr
  getmetatable(u).__gc = function (o)
    stderr:write'.'
    newproxy(o)
  end
end

-- LuaJIT: Adapt tests for testing with out-of-source build.
-- See the comment in <test/luajit-test-init.lua>.
local f = assert(_loadfile('gc.lua'))
f()
_dofile('db.lua')
assert(_dofile('calls.lua') == deep and deep)
_dofile('strings.lua')
_dofile('literals.lua')
assert(_dofile('attrib.lua') == 27)
assert(_dofile('locals.lua') == 5)
_dofile('constructs.lua')
_dofile('code.lua')
do
  local f = coroutine.wrap(assert(_loadfile('big.lua')))
  assert(f() == 'b')
  assert(f() == 'a')
end
_dofile('nextvar.lua')
_dofile('pm.lua')
_dofile('api.lua')
assert(_dofile('events.lua') == 12)
_dofile('vararg.lua')
_dofile('closure.lua')
_dofile('errors.lua')
_dofile('math.lua')
_dofile('sort.lua')
assert(_dofile('verybig.lua') == 10); collectgarbage()
_dofile('files.lua')

if #msgs > 0 then
  print("\ntests not performed:")
  for i=1,#msgs do
    print(msgs[i])
  end
  print()
end

print("final OK !!!")
print('cleaning all!!!!')

debug.sethook(function (a) assert(type(a) == 'string') end, "cr")

local _G, collectgarbage, showmem, print, format, clock =
      _G, collectgarbage, showmem, print, format, os.clock

local a={}
for n in pairs(_G) do a[n] = 1 end
a.tostring = nil
a.___Glob = nil
for n in pairs(a) do _G[n] = nil end

a = nil
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage()
collectgarbage();showmem()

print(format("\n\ntotal time: %.2f\n", clock()-c))
