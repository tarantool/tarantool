local luzer = require("luzer")

local has_memprof, memprof = pcall(require, "misc.memprof")
if not has_memprof then
  print("Unsupported version.")
  os.exit(0)
end

local MAX_STR_LEN = 1e5

local function TestOneInput(buf)
  local fdp = luzer.FuzzedDataProvider(buf)
  local chunk = fdp:consume_string(MAX_STR_LEN)
  -- LuaJIT ASSERT lj_bcread.c:123: bcread_byte: buffer read
  -- overflow.
  local func = load(chunk, "luzer", "t")

  assert(memprof.start("/dev/null"))
  pcall(func)
  assert(memprof.stop())
end

local args = {
  artifact_prefix = "misc_memprof_",
}
luzer.Fuzz(TestOneInput, nil, args)
