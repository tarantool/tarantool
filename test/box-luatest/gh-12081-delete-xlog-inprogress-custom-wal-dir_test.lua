local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    -- Start the server so workdir is created.
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_function_delete_inprogress_custom_location = function(g)
    local fio = require("fio")
    local wal_dir = fio.pathjoin(g.server.workdir, "custom_wal_dir")
    fio.mktree(wal_dir)
    g.server:restart(
        {box_cfg = {wal_dir = wal_dir}}, {wait_until_ready = true}
    )
    g.server:exec(function()
        box.space._schema:replace{'test'}
    end)
    g.server:stop()
    -- The magical vclock below is due to the server restart.
    local xlog_path = fio.pathjoin(
        wal_dir, "00000000000000000002.xlog"
    )
    t.assert(fio.path.exists(xlog_path))
    local inprogress_path = fio.pathjoin(
        wal_dir, "00000000000000000002.xlog.inprogress"
    )
    fio.rename(xlog_path, inprogress_path)
    t.assert(fio.path.exists(inprogress_path))
    g.server:restart({}, {wait_until_ready = true})
    g.server:exec(function()
        box.space._schema:replace{'test'}
    end)
    t.assert_not(fio.path.exists(inprogress_path))
end
