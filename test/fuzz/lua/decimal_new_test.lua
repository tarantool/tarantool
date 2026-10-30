local luzer = require("luzer")
local decimal = require("decimal")

local function TestOneInput(buf)
  local ok, res = pcall(decimal.new, buf)
  if not ok then
    return
  end
  -- Decimal is a valid number, if `decimal.new()` finished
  -- successfully.
  assert(res ~= nil)
  assert(decimal.is_decimal(res) == true)
end

local args = {
  artifact_prefix = "decimal_new_",
}
luzer.Fuzz(TestOneInput, nil, args)
