local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- The last replication_linearizable_quorum value the server logged.
local function logged_linearizable_quorum(cg)
    local file = fio.open(cg.server.log_file, {'O_RDONLY'})
    local text = file:read()
    file:close()
    local quorum
    for q in text:gmatch('update replication_linearizable_quorum = (%d+)') do
        quorum = tonumber(q)
    end
    return quorum
end

-- The default quorum formulas are computed without Lua. Check the quorums
-- they give for every number of registered replicas N.
g.test_default_formulas = function(cg)
    cg.server:exec(function()
        t.assert_equals(box.cfg.replication_synchro_quorum, 'N / 2 + 1')
        t.assert_equals(box.cfg.replication_linearizable_quorum, 'N - Q + 1')
        t.assert_equals(box.info.synchro.quorum, 1)
    end)
    for n = 2, 31 do
        local synchro_quorum = math.floor(n / 2 + 1)
        cg.server:exec(function(id, quorum)
            box.space._cluster:insert{id, require('uuid').str()}
            t.assert_equals(box.info.synchro.quorum, quorum)
        end, {n, synchro_quorum})
        t.helpers.retrying({}, function()
            t.assert_equals(logged_linearizable_quorum(cg),
                            n - synchro_quorum + 1)
        end)
    end
end
