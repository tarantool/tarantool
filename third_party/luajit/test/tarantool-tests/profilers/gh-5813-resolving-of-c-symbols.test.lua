local tap = require("tap")
local test = tap.test("gh-5813-resolving-of-c-symbols"):skipcond({
  ["Memprof is implemented for x86_64 only"] = jit.arch ~= "x86" and
                                               jit.arch ~= "x64",
  ["Memprof is implemented for Linux only"] = jit.os ~= "Linux",
  -- See also https://github.com/LuaJIT/LuaJIT/issues/606.
  ["Disabled due to LuaJIT/LuaJIT#606"] = os.getenv("LUAJIT_TABLE_BUMP"),
  ["Sysprof is disabled"] = os.getenv("LUAJIT_DISABLE_SYSPROF")
})

test:plan(5)

jit.off()
-- XXX: Run JIT tuning functions in a safe frame to avoid errors
-- thrown when LuaJIT is compiled with JIT engine disabled.
pcall(jit.flush)

local bufread = require "utils.bufread"
local symtab = require "utils.symtab"
local testboth = require "resboth"
local testhash = require "reshash"
local testgnuhash = require "resgnuhash"
local profilename = require("utils").tools.profilename

local TMP_BINFILE = profilename("memprofdata.tmp.bin")

local function tree_contains(node, name)
  if node == nil then
    return false
  elseif node.value.name == name then
    return true
  else
    return tree_contains(node.left, name) or tree_contains(node.right, name)
  end
end

local function generate_output(filename, payload)
  local res, err = misc.memprof.start(filename)
  -- Should start successfully.
  assert(res, err)

  for _ = 1, 100 do
    payload()
  end

  res, err = misc.memprof.stop()
  -- Should stop successfully.
  assert(res, err)
end

local function generate_parsed_symtab(payload)
  local res, err = pcall(generate_output, TMP_BINFILE, payload)

  -- Want to cleanup carefully if something went wrong.
  if not res then
    os.remove(TMP_BINFILE)
    error(err)
  end

  local reader = bufread.new(TMP_BINFILE)
  local symbols = symtab.parse(reader)

  -- We don't need it any more.
  os.remove(TMP_BINFILE)

  return symbols
end

local symbols = generate_parsed_symtab(function()
  -- That Lua module is required here to trigger the `luaopen_os`,
  -- which is not stripped.
  require("resstripped").allocate_string()
end)

-- Static symbols resolution.
test:ok(tree_contains(symbols.cfunc, "luaopen_os"))

-- Dynamic symbol resolution. Newly loaded symbol resolution.
test:ok(tree_contains(symbols.cfunc, "allocate_string"))

-- .hash style symbol table.
symbols = generate_parsed_symtab(testhash.allocate_string)
test:ok(tree_contains(symbols.cfunc, "allocate_string"))

-- .gnu.hash style symbol table.
symbols = generate_parsed_symtab(testgnuhash.allocate_string)
test:ok(tree_contains(symbols.cfunc, "allocate_string"))

-- Both symbol tables.
symbols = generate_parsed_symtab(testboth.allocate_string)
test:ok(tree_contains(symbols.cfunc, "allocate_string"))

-- FIXME: There is one case that is not tested -- shared objects,
-- which have neither .symtab section nor .dynsym segment. It is
-- unclear how to perform a test in that case, since it is
-- impossible to load Lua module written in C if it doesn't have
-- a .dynsym segment.

test:done(true)
