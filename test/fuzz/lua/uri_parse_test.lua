local uri = require("uri")
local luzer = require("luzer")

local function TestOneInput(buf)
  local url = uri.parse(buf)
  if type(url) == "table" and
    url ~= nil then
    local str = uri.format(url)
    assert(str)
  end
end

local args = {
  artifact_prefix = "uri_parse_",
}
luzer.Fuzz(TestOneInput, nil, args)
