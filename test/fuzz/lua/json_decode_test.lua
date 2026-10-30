-- https://github.com/tarantool/tarantool/issues/4366

local json = require("json")
local luzer = require("luzer")

local function TestOneInput(buf)
  local ok, obj = pcall(json.decode, buf)
  if ok then
    assert(json.encode(obj) ~= nil)
  end
end

local args = {
  artifact_prefix = "json_decode_",
}
luzer.Fuzz(TestOneInput, nil, args)
