local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

-- Checks that unsupported MVCC features cannot be enabled along with MVCC.
g.test_unsupported_features = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        box.schema.func.create('test', {
            is_deterministic = true,
            is_sandboxed = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        local errmsg = "Memtx MVCC engine does not support functional indexes"
        t.assert_error_msg_content_equals(errmsg, function()
            s:create_index('test_secondary', {
                func = 'test',
                parts = {{1, 'unsigned'}},
            })
        end)

        errmsg = "Memtx MVCC engine does not support multikey indexes"
        t.assert_error_msg_content_equals(errmsg, function()
            s:create_index('test_secondary', {
                parts = {{2, 'unsigned', path = '[*]'}},
            })
        end)
    end)
end

-- Checks that recovery of unsupported MVCC features fails.
g.test_recovery_with_unsupported_features = function(cg)
    local server_log_path = g.server:exec(function()
        return rawget(_G, 'box_cfg_log_file') or box.cfg.log
    end)

    -- Check functional multikey index.
    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = false}})
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        box.schema.func.create('test', {
            is_deterministic = true,
            is_sandboxed = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        s:create_index('test_secondary', {
            func = 'test',
            parts = {{1, 'unsigned'}},
        })
    end)

    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = true}},
        {wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert(g.server:grep_log(
            "F> can't initialize storage: Memtx MVCC engine does not support" ..
            " functional indexes", nil, {filename = server_log_path}))
    end)

    -- Check regular multikey index.
    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = false}})
    cg.server:exec(function()
        box.space.test.index.test_secondary:drop()
        box.space.test:create_index('test_secondary', {
            parts = {{2, 'unsigned', path = '[*]'}},
        })
        -- We need to do a snapshot to make the definition (and, potentially,
        -- data inserted to the index) disappear from snapshot and WAL.
        box.snapshot()
    end)

    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = true}},
        {wait_until_ready = false})
    t.helpers.retrying({}, function()
        t.assert(g.server:grep_log(
            "F> can't initialize storage: Memtx MVCC engine does not support" ..
            " multikey indexes", nil, {filename = server_log_path}))
    end)

    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = false}})
    cg.server:exec(function()
        box.space.test.index.test_secondary:drop()
        -- We need to do a snapshot to make the definition (and, potentially,
        -- data inserted to the index) disappear from snapshot and WAL.
        box.snapshot()
    end)

    cg.server:restart({box_cfg = {memtx_use_mvcc_engine = true}})
end
