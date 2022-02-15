local cluster = require('test.luatest_helpers.cluster')
local fio = require('fio')
local t = require('luatest')

local g = t.group('gh-6568-replica-initial-join-removal-of-compacted-run-files')

local s_id = 0
g.before_all(function()
    g.cluster = cluster:new({})
    g.master = g.cluster:build_and_add_server({alias = 'master'})

    local replica_box_cfg = {
        vinyl_memory = 128,
        replication  = { g.master.net_box_uri },
        read_only    = true,
    }
    g.replica = g.cluster:build_and_add_server({alias   = 'replica',
                                                box_cfg = replica_box_cfg})

    g.master:start()
    s_id = g.master:exec(function()
        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')

        for i = 1, 64 do s:insert{i, ('x'):rep(63)} end

        return s.id
    end)

    g.replica:start()
end)

g.after_all(function()
    g.cluster:drop()
end)

g.test_replication_compaction_cleanup = function()
    local replica_vinyl_dir = g.replica:exec(function()
        local fio = require('fio')

        return fio.cwd()
    end)
    local replica_index_dir = fio.pathjoin(replica_vinyl_dir,
                                           ('%d/0/'):format(s_id))
    local observed_sz = 0
    for _, file_name in pairs(fio.listdir(replica_index_dir)) do
        if file_name:find('.run$') then
            local file_path = fio.pathjoin(replica_index_dir, file_name)
            observed_sz = observed_sz + fio.stat(file_path).size
        end
    end
    local expected_sz = g.replica:exec(function()
        return box.stat.vinyl().disk.data
    end)
    local sz_diff = observed_sz - expected_sz
    local msg = 'the total actual size of run files directories of replica ' ..
                'must not differ from box.stat.vinyl().disk.data by more ' ..
                'than %.1f%%'
    local perm_rel_sz_diff = 1
    msg = msg:format(perm_rel_sz_diff * 100)
    t.assert_le(sz_diff / expected_sz, perm_rel_sz_diff, msg)
end
