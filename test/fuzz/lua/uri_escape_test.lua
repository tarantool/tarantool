local uri = require("uri")
local luzer = require("luzer")

local function TestOneInput(buf)
    local url = uri.unescape(buf)
    if url ~= nil then
        assert(uri.escape(url) == buf)
    end
end

local args = {
    artifact_prefix = "uri_escape_",
}
luzer.Fuzz(TestOneInput, nil, args)
