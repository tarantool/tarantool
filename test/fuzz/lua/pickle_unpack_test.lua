local pickle = require("pickle")
local luzer = require("luzer")

local STRING_MAX_LEN = 1e6

local function err_handler(ignored_msgs)
  return function(error_msg)
    for _, ignored_msg in ipairs(ignored_msgs) do
      local x, _ = string.find(error_msg, ignored_msg)
      if x then break end
    end
  end
end

local unpack_ignored_msgs = {
  "too many results to unpack",
}
local unpack_err_handler = err_handler(unpack_ignored_msgs)

local function TestOneInput(buf)
  local fdp = luzer.FuzzedDataProvider(buf)
  local binary_string_len = fdp:consume_integer(1, STRING_MAX_LEN)
  local binary_string = fdp:consume_string(binary_string_len)
  local format_string_len = fdp:consume_integer(1, STRING_MAX_LEN)
  local format_string = fdp:consume_string(format_string_len)
  local res = {pcall(pickle.unpack, format_string, binary_string)}
  if res[1] then
    table.remove(res, 1)
    -- Check for the error "too many results to unpack".
    local ok = xpcall(unpack, unpack_err_handler, res)
    if not ok then return end

    local packed = pickle.pack(format_string, unpack(res))
    assert(#packed == #binary_string)
  end
end

local args = {
  artifact_prefix = "pickle_unpack_",
}
luzer.Fuzz(TestOneInput, nil, args)
