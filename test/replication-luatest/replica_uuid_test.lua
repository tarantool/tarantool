local server = require('luatest.server')
local t = require('luatest')

local g = t.group(nil, t.helpers.matrix({
    memtx_use_mvcc_engine = {false, true},
}))

g.before_each(function(cg)
    cg.server = server:new({box_cfg = cg.params})
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_insert_checks_decoded_uuid = function(cg)
    cg.server:exec(function()
        local cluster = box.space._cluster
        local uuid = 'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa'
        cluster:insert{2, uuid}
        t.assert_error_msg_contains(
            'registration with an already registered UUID',
            cluster.insert, cluster, {3, uuid:upper()})
        t.assert_equals(cluster:get{3}, nil)
        t.assert_equals(box.info.replication[2].uuid, uuid)
        t.assert_equals(box.info.replication[3], nil)
    end)
end
