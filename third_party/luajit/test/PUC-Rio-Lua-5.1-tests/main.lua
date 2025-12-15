# testing special comment on first line

print ("testing lua.c options")

assert(os.execute() ~= 0)   -- machine has a system command

prog = os.tmpname()
otherprog = os.tmpname()
out = os.tmpname()

do
  local i = 0
  while arg[i] do i=i-1 end
  -- LuaJIT: remove framing '"' to be able to run an extended
  -- command containing '"' and run other test suites.
  progname = arg[i+1]
end
print(progname)

local prepfile = function (s, p)
  p = p or prog
  io.output(p)
  io.write(s)
  assert(io.close())
end

-- Taken from PUC-Rio Lua 5.2 test suite.
-- See comment for checkprogout().
function getoutput ()
  io.input(out)
  local t = io.read("*a")
  io.input():close()
  assert(os.remove(out))
  return t
end

-- Version and status are printed in stdout instead of stderr
-- since LuaJIT-2.0.0-beta11 (as it is not an error message).
-- See commit 0bd1a66f2f055211ef55834ccebca3b82d03c735
-- (Print version and JIT status to stdout, not stderr.).
-- This behavior is the same as in Lua 5.2.
-- See also https://github.com/tarantool/tarantool/issues/5687.
-- This function is adapted from PUC-Rio Lua 5.2 test suite.
-- It is used for test commands with -i flag.
function checkprogout (s)
  local t = getoutput()
  for line in string.gmatch(s, ".-\n") do
    assert(string.find(t, line, 1, true))
  end
end

function checkout (s)
  local t = getoutput()
  if s ~= t then print(string.format("'%s' - '%s'\n", s, t)) end
  assert(s == t)
  return t
end

function auxrun (...)
  local s = string.format(...)
  s = string.gsub(s, "lua", progname, 1)
  return os.execute(s)
end

function RUN (...)
  assert(auxrun(...) == 0)
end

function NoRun (...)
  print("\n(the next error is expected by the test)")
  assert(auxrun(...) ~= 0)
end

-- test 2 files
prepfile("print(1); a=2")
prepfile("print(a)", otherprog)
RUN("lua -l %s -l%s -lstring -l io %s > %s", prog, otherprog, otherprog, out)
checkout("1\n2\n2\n")

-- LuaJIT: test file is adapted for LuaJIT's test system, see
-- the comment near `progname` initialization.
local a = [[
  assert(table.getn(arg) == 3 and arg[1] == 'a' and
         arg[2] == 'b' and arg[3] == 'c')
  assert(arg[-1] == '--' and arg[-2] == "-e " and arg[-3] == '%s')
  assert(arg[4] == nil and arg[-4] == nil)
  local a, b, c = ...
  assert(... == 'a' and a == 'a' and b == 'b' and c == 'c')
]]
a = string.format(a, progname)
prepfile(a)
-- FIXME: Unlike LuaJIT, Tarantool doesn't store the given
-- CLI flags in `arg`, so the table has the following layout:
-- * arg[-1] -- the binary name
-- * arg[0]  -- the script name
-- * arg[N]  -- the script argument for all N in [1, #arg]
-- Test is disabled for the Tarantool binary.
-- RUN('lua "-e " -- %s a b c', prog)

-- test 'arg' availability in libraries
-- LuaJIT: LuaJIT v2.1.0-beta3 has extension from Lua 5.3:
-- The argument table `arg` can be read (and modified)
-- by `LUA_INIT` and `-e` chunks.
-- See commit 92d9ff211ae864777a8580b5a7326d5f408161ce
-- (Set arg table before evaluating LUA_INIT and -e chunks.).
-- See also https://github.com/tarantool/tarantool/issues/5686.
-- In Lua 5.3 this feature was introduced via commit
-- 23f0ff95177eda2e0a80e3a48562cc6837705735.
-- Test is adapted from PUC-Rio Lua 5.3 test suite.
prepfile"assert(arg)"
prepfile("assert(arg)", otherprog)
RUN('env LUA_PATH="?;;" lua -l%s - < %s', prog, otherprog)

prepfile""
RUN("lua - < %s > %s", prog, out)
checkout("")

-- test many arguments
prepfile[[print(({...})[30])]]
RUN("lua %s %s > %s", prog, string.rep(" a", 30), out)
checkout("a\n")

RUN([[lua "-eprint(1)" -ea=3 -e "print(a)" > %s]], out)
checkout("1\n3\n")

prepfile[[
  print(
1, a
)
]]
RUN("lua - < %s > %s", prog, out)
checkout("1\tnil\n")

-- FIXME: The following chunk is disabled for Tarantool since
-- its interactive shell doesn't support '=' at the beginning
-- of statements.
-- Here is an example:
-- $ luajit
-- LuaJIT 2.1.0-beta3 -- Copyright (C) 2005-2017 Mike Pall. http://luajit.org/
-- JIT: ON SSE2 SSE3 SSE4.1 BMI2 fold cse dce fwd dse narrow loop abc sink fuse
-- > = (6*2-6) -- ===
-- 6

-- $ tarantool
-- Tarantool 2.10.0-beta1-80-g201905544
-- type 'help' for interactive help
-- tarantool> = (6*2-6) -- ===
-- ---
-- - error: ' (6*2-6) -- ===:1: unexpected symbol near ''='''
-- ...
if not _TARANTOOL then
-- Test is adapted from PUC-Rio Lua 5.2 test suite.
-- See comment for checkprogout().
  prepfile[[
= (6*2-6) -- ===
a
= 10
print(a)
= a]]
  RUN([[lua -e"_PROMPT='' _PROMPT2=''" -i < %s > %s]], prog, out)
  checkprogout("6\n10\n10\n\n")

-- Test is adapted from PUC-Rio Lua 5.2 test suite.
-- See comment for checkprogout().
  prepfile("a = [[b\nc\nd\ne]]\n=a")
  print(prog)
  RUN([[lua -e"_PROMPT='' _PROMPT2=''" -i < %s > %s]], prog, out)
  checkprogout("b\nc\nd\ne\n\n")

-- Test is adapted from PUC-Rio Lua 5.2 test suite.
-- See comment for checkprogout().
  prompt = "alo"
  prepfile[[ --
a = 2
]]
  RUN([[lua "-e_PROMPT='%s'" -i < %s > %s]], prompt, prog, out)
  local t = getoutput()
  assert(string.find(t, prompt .. ".*" .. prompt .. ".*" .. prompt))

  s = [=[ --
function f ( x )
  local a = [[
xuxu
]]
  local b = "\
xuxu\n"
  if x == 11 then return 1 , 2 end  --[[ test multiple returns ]]
  return x + 1
  --\\
end
=( f( 10 ) )
assert( a == b )
=f( 11 )  ]=]
  s = string.gsub(s, ' ', '\n\n')
  prepfile(s)
  RUN([[lua -e"_PROMPT='' _PROMPT2=''" -i < %s > %s]], prog, out)
  checkprogout("11\n1\t2\n\n")
end -- not _TARANTOOL

prepfile[[#comment in 1st line without \n at the end]]
RUN("lua %s", prog)

-- Loading bytecode with an extra header (BOM or "#")
-- is disabled for security reasons since LuaJIT-2.0.0-beta10.
-- For more information see comment for `lj_lex_setup()`
-- in <src/lj_lex.c>.
-- Also see commit 53a285c0c3544ff5dea7c67b741c3c2d06d22b47
-- (Disable loading bytecode with an extra header (BOM or #!).).
-- See also https://github.com/tarantool/tarantool/issues/5691.
-- The test is adapted to LuaJIT behavior.
prepfile(string.dump(loadstring("print(1)")))
RUN("lua %s > %s", prog, out)
checkout("1\n")

-- close Lua with an open file
prepfile(string.format([[io.output(%q); io.write('alo')]], out))
RUN("lua %s", prog)
checkout('alo')

assert(os.remove(prog))
assert(os.remove(otherprog))
assert(not os.remove(out))

RUN("lua -v")

-- FIXME: Tarantool returns zero status at exit with -h option,
-- unlike Lua or LuaJIT does.
-- The test is disabled for Tarantool binary.
-- NoRun("lua -h")
NoRun("lua -e")
NoRun("lua -e a")
NoRun("lua -f")

print("OK")
