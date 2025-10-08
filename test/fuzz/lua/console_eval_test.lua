-- "Three bytes of death"
-- https://github.com/tarantool/tarantool/issues/4773

local luzer = require("luzer")
local console = require("console")

local function TestOneInput(buf)
  console.eval(buf)
end

local args = {
  artifact_prefix = "console_eval_",
  -- LuaJIT ASSERT bcread_byte: buffer read overflow
  only_ascii = 1,
}
luzer.Fuzz(TestOneInput, nil, args)
