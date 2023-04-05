-- https://github.com/tarantool/tarantool/issues/3866
-- https://github.com/tarantool/tarantool/issues/3861

local luzer = require("luzer")

box.cfg({})

local function TestOneInput(buf)
  box.execute(buf)
end

local args = {
  artifact_prefix = "box_execute_",
}
luzer.Fuzz(TestOneInput, nil, args)
