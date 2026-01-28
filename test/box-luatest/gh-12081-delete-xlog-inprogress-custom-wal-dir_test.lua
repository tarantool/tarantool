local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(g)
    g.server = server:new()
    g.server:start()
end)

g.after_all(function(g)
    g.server:drop()
end)

g.test_function_delete_inprogress_custom_location = function(g)
    local fio = require("fio")
    local wal_dir = fio.pathjoin(g.server.workdir, "custom_wal_dir")
    local inprogress_path = fio.pathjoin(
        wal_dir, "00000000000000000001.xlog.inprogress"
    )
    fio.mktree(wal_dir)
    local file = fio.open(inprogress_path, {'O_CREAT', 'O_WRONLY'})
    file:close()
    t.assert(fio.path.exists(inprogress_path))
    g.server:restart(
        {box_cfg = {wal_dir = wal_dir}}, {wait_until_ready = true}
    )
    t.assert_not(fio.path.exists(inprogress_path))
end
