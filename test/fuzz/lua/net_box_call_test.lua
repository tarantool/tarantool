--[[
"One byte of death"
https://github.com/tarantool/tarantool/issues/6781
]]

local luzer = require("luzer")
local net_box = require("net.box")

local function TestOneInput(buf)
    -- The test generates memtx snapshot, cleanup before the test.
    os.execute("rm -f *.snap")
    local socket_path = os.tmpname()
    box.cfg({
        listen = socket_path,
        -- Suppress Tarantool diagnostic messages.
        log_level = "warn",
    })
    local conn = net_box.connect(socket_path)
    pcall(conn.call, conn, buf)
end

local args = {
    artifact_prefix = "net_box_call_",
}
luzer.Fuzz(TestOneInput, nil, args)
