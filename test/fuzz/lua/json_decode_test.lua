-- https://github.com/tarantool/tarantool/issues/4366

local json = require("json")
local luzer = require("luzer")
local math = require("math")

local function TestOneInput(buf)
    local ok, obj = pcall(json.decode, buf)
    if obj == math.inf or
       obj == 0/0 then
        return -1
    end
    if ok == true then
        assert(json.encode(obj) ~= nil)
    end
end

local args = {
    artifact_prefix = "json_decode_",
}
luzer.Fuzz(TestOneInput, nil, args)
