local t = require('luatest')
local cluster = require('luatest.replica_set')
local fio = require('fio')

local g = t.group('gh_7932')

g.before_each(function(cg)
    cg.cluster = cluster:new({})
    cg.server = cg.cluster:build_and_add_server{
        alias = 'server',
        box_cfg = {
            instance_uuid = '1a685e63-24cd-4fc5-9532-bf85f649e0ab',
        },
    }
    cg.cluster:start()
end)

g.after_each(function(cg)
    cg.cluster:drop()
end)

-- Check that transactions with both local and global spaces recover fine
-- with this patch
g.test_local_and_global_spaces_recover = function(cg)
    cg.server:exec(function()
        box.schema.space.create('loc', {is_local = true})
        box.space.loc:create_index('pk')
        box.schema.space.create('glob')
        box.space.glob:create_index('pk')
        box.begin()
        for i = 1,3 do
            box.space.glob:replace{i, box.info.replication[1].id}
            box.space.loc:replace{i, 0}
        end
        box.commit()
    end)

    t.assert_equals(cg.server:exec(function()
        return box.space.glob:select()
    end), {{1, 1}, {2, 1}, {3, 1}})
    t.assert_equals(cg.server:exec(function()
        return box.space.loc:select()
    end), {{1, 0}, {2, 0}, {3, 0}})

    cg.server:stop()

    cg.server.box_cfg.force_recovery = true
    cg.server:start()
    -- Check that the entire transaction has been applied
    t.assert_equals(cg.server:exec(function()
        return box.space.glob:select()
    end), {{1, 1}, {2, 1}, {3, 1}})
    t.assert_equals(cg.server:exec(function()
        return box.space.loc:select()
    end), {{1, 0}, {2, 0}, {3, 0}})
end


g.test_good_xlog_with_force_recovery = function(cg)
    -- Delete all *.xlogs on the server
    cg.server:stop()
    local glob = fio.pathjoin(cg.server.workdir, '*.xlog')
    local xlog = fio.glob(glob)
    for _, file in pairs(xlog) do fio.unlink(file) end
    -- Copying the prepared log to the server
    xlog = 'test/replication-luatest/gh_7932_data/good_xlog/'..
           '00000000000000000000.xlog'
    fio.copyfile(xlog, cg.server.workdir)

    cg.server.box_cfg.force_recovery = true
    cg.server:start()
    -- The first transaction is applied and after the second transaction
    -- is applied.
    t.assert_equals(cg.server:exec(function()
        return box.space.test:select()
    end), {{1, 2}, {2, 2}, {3, 2}})
end

g.test_good_xlog_without_force_recovery = function(cg)
    -- Delete all *.xlogs on the server
    cg.server:stop()
    local glob = fio.pathjoin(cg.server.workdir, '*.xlog')
    local xlog = fio.glob(glob)
    for _, file in pairs(xlog) do fio.unlink(file) end
    -- Copying the prepared log to the server
    xlog = 'test/replication-luatest/gh_7932_data/good_xlog/'..
           '00000000000000000000.xlog'
    fio.copyfile(xlog, cg.server.workdir)

    cg.server.box_cfg.force_recovery = false
    cg.server:start({wait_until_ready = false})
    local logfile = fio.pathjoin(cg.server.workdir, 'server.log')
    -- The transactions aren't applied without force_recovery
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log("error at request", nil,
            {filename = logfile}), "{type: 'REPLACE', replica_id: 2, "..
            "lsn: 32050, space_id: 512, index_id: 0, tuple: [1, 2]}")
        t.assert(cg.server:grep_log("XlogError", nil,
            {filename = logfile}), "found a next transaction with the "..
            "previous one not yet committed")
    end)
end

g.test_bad_xlog_with_force_recovery = function(cg)
    -- Delete all *.xlogs on the server
    cg.server:stop()
    local glob = fio.pathjoin(cg.server.workdir, '*.xlog')
    local xlog = fio.glob(glob)
    for _, file in pairs(xlog) do fio.unlink(file) end
    -- Copying the prepared log to the server
    xlog = 'test/replication-luatest/gh_7932_data/bad_xlog/'..
           '00000000000000000000.xlog'
    fio.copyfile(xlog, cg.server.workdir)

    cg.server.box_cfg.force_recovery = true
    cg.server:start()
    local logfile = fio.pathjoin(cg.server.workdir, 'server.log')

    -- The second transaction cannot be applied because the first global
    -- row in the transaction has an LSN/TSN mismatch.
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log("error at request", nil,
            {filename = logfile}), "{type: 'REPLACE', replica_id: 2, "..
            "lsn: 32051, space_id: 512, index_id: 0, tuple: [2, 2]}")
        t.assert(cg.server:grep_log("skipping row {2: 32052}", nil,
            {filename = logfile}), "skipping row")
        t.assert(cg.server:grep_log("XlogError", nil,
            {filename = logfile}), "found a first global row in a "..
            "transaction with LSN/TSN mismatch")
    end)

    -- Only the first transaction is applied
    t.assert_equals(cg.server:exec(function()
        return box.space.test:select()
    end), {{1, 3}, {2, 3}, {3, 3}})
end

g.test_not_finished_transaction = function(cg)
    -- Delete all *.xlogs on the server
    cg.server:stop()
    local glob = fio.pathjoin(cg.server.workdir, '*.xlog')
    local xlog = fio.glob(glob)
    for _, file in pairs(xlog) do fio.unlink(file) end
    -- Copying the prepared log to the server
    xlog = 'test/replication-luatest/gh_7932_data/not_finished_xlog/'..
           '00000000000000000000.xlog'
    fio.copyfile(xlog, cg.server.workdir)

    cg.server.box_cfg.force_recovery = true
    cg.server:start()
    local logfile = fio.pathjoin(cg.server.workdir, 'server.log')

    -- The second transaction is not completed
    t.helpers.retrying({}, function()
        t.assert(cg.server:grep_log("XlogError", nil,
            {filename = logfile}), "found a not finished transaction " ..
            "in the log")
    end)

    -- Only the first transaction is applied
    t.assert_equals(cg.server:exec(function()
        return box.space.test:select()
    end), {{1, 3}, {2, 3}, {3, 3}})
end
