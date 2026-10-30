-- https://www.tarantool.io/en/doc/latest/reference/reference_lua/csv/

local csv = require("csv")
local luzer = require("luzer")

local function TestOneInput(buf)
  local ok, res = pcall(csv.load, buf)
  if ok == true then
    assert(res ~= nil)
  end
  res = csv.dump(res)
  assert(ok == true)
  assert(res)
end

local args = {
  artifact_prefix = "csv_load_",
}
luzer.Fuzz(TestOneInput, nil, args)
