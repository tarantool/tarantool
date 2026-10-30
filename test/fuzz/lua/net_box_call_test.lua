--[[
"One byte of death"
https://github.com/tarantool/tarantool/issues/6781
]]

local luzer = require("luzer")
local net_box = require("net.box")
local fio = require("fio")

local function rmtree(path)
  if (fio.path.is_file(path) or fio.path.is_link(path)) then
    fio.unlink(path)
    return
  end
  if fio.path.is_dir(path) then
    for _, p in pairs(fio.listdir(path)) do
      rmtree(fio.pathjoin(path, p))
    end
  end
end

local function cleanup_dir(dir)
  if dir ~= nil then
    rmtree(dir)
    dir = nil -- luacheck: ignore
  end
end

local TEST_DIR = "net_box_call_test_dir"

local function TestOneInput(buf)
  -- The test generates memtx snapshot, cleanup before the test.
  if fio.path.exists(TEST_DIR) then
    cleanup_dir(TEST_DIR)
  else
    fio.mkdir(TEST_DIR)
  end
  local socket_path = os.tmpname()
  box.cfg({
    listen = socket_path,
    work_dir = TEST_DIR,
    -- Suppress Tarantool diagnostic messages.
    log_level = "warn",
  })
  local conn = net_box.connect(socket_path)
  pcall(conn.call, conn, buf)
  conn:close()
end

local args = {
  artifact_prefix = "net_box_call_",
}
luzer.Fuzz(TestOneInput, nil, args)
