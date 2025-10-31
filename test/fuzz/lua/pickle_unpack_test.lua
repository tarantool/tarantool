local pickle = require("pickle")
local luzer = require("luzer")

local function TestOneInput(buf)
    local ok, unpacked = pcall(pickle.unpack, buf)
    if ok == true then
        local packed = pickle.pack(unpacked)
        assert(#packed == #buf)
    end
end

local args = {
    artifact_prefix = "pickle_unpack_",
}
luzer.Fuzz(TestOneInput, nil, args)
