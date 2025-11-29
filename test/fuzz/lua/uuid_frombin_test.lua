local uuid = require("uuid")
local luzer = require("luzer")

local function TestOneInput(buf)
  local ok, res = pcall(uuid.frombin, buf)
  if ok then
    assert(res ~= nil)
    assert(uuid.is_uuid(res))
    assert(res:str())
  end
end

local args = {
  artifact_prefix = "uuid_frombin_",
}
luzer.Fuzz(TestOneInput, nil, args)
