local luzer = require("luzer")
local decimal = require("decimal")

local function TestOneInput(buf)
    local ok, res = pcall(decimal.new, buf)
    if ok == false then
        return
    end
    assert(res ~= nil)
    assert(decimal.is_decimal(res) == true)
    assert(res - res == 0)
end

local args = {
    artifact_prefix = "decimal_new_",
}
luzer.Fuzz(TestOneInput, nil, args)
