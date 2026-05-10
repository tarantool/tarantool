local pickle = require("pickle")
local luzer = require("luzer")

local STRING_MAX_LEN = 1e6

local function TestOneInput(buf)
  local fdp = luzer.FuzzedDataProvider(buf)
  local binary_string_len = fdp:consume_integer(1, STRING_MAX_LEN)
  local binary_string = fdp:consume_string(binary_string_len)
  local format_string_len = fdp:consume_integer(1, STRING_MAX_LEN)
  local format_string = fdp:consume_string(format_string_len)
  local res = {pcall(pickle.unpack, format_string, binary_string)}
  if res[1] then
    table.remove(res, 1)
    local packed = pickle.pack(format_string, unpack(res))
    assert(#packed == #binary_string)
  end
end

local args = {
  artifact_prefix = "pickle_unpack_",
}
luzer.Fuzz(TestOneInput, nil, args)
