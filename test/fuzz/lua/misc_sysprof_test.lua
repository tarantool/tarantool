local luzer = require("luzer")
local sysprof = require("misc.sysprof")

if not sysprof.enabled then
  print("sysprof is unsupported or disabled")
  os.exit(0)
end

local SYSPROF_DEFAULT_INTERVAL = 1 -- ms
local MAX_STR_LEN = 1e5

local function TestOneInput(buf)
  local fdp = luzer.FuzzedDataProvider(buf)
  local chunk = fdp:consume_string(MAX_STR_LEN)
  -- LuaJIT ASSERT lj_bcread.c:123: bcread_byte: buffer read
  -- overflow.
  local func = load(chunk, "luzer", "t")
  local sysprof_mode = fdp:oneof({"D", "L", "C"})

  assert(sysprof.start({
    interval = SYSPROF_DEFAULT_INTERVAL,
    mode = sysprof_mode,
    path = "/dev/null",
  }))
  pcall(func)
  sysprof.report()
  sysprof.stop()
end

local args = {
  artifact_prefix = "misc_sysprof_",
}
luzer.Fuzz(TestOneInput, nil, args)
