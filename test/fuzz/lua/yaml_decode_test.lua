-- https://github.com/tarantool/tarantool/issues/4773 \x36\x00\x80

local yaml = require("yaml")
local luzer = require("luzer")

local function TestOneInput(buf)
  local ok, res = pcall(yaml.decode, buf)
  if ok == false then
    return
  end
  yaml.encode(res)
end

local args = {
  artifact_prefix = "yaml_decode_",
}
luzer.Fuzz(TestOneInput, nil, args)
