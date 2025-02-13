local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

local wal_max_size = 512
local max_wal_files = 10
local tuples_per_batch = 8

g.before_all(function(g)
    g.server = server:new({box_cfg = {
        wal_max_size = wal_max_size,
        checkpoint_wal_threshold = max_wal_files * wal_max_size,
        checkpoint_count = 1,
        wal_mode = 'fsync',  -- we measure xlog files sizes
        checkpoint_interval = 0,
    }})
    g.server:start()
    g.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk', { sequence = true })
    end)
end)

local function define_helpers(server)
    server:exec(function()
        rawset(_G, 'measure_xlog_size_sum', function()
            local fio = require('fio')
            local result = 0
            for _, v in pairs(fio.listdir(box.cfg.wal_dir)) do
                if string.find(v, "%.xlog") then
                    local fname = fio.pathjoin(box.cfg.wal_dir, v)
                    result = result + fio.stat(fname).size
                end
            end
            return result
        end)
        rawset(_G, 'manual_gc_trigger', function()
            -- that should erase all existed xlogs
            for _=1,box.cfg.checkpoint_count + 1 do
                box.snapshot()
            end
        end)
    end)
end

g.after_all(function(g)
    g.server:drop()
end)

g.before_each(function(g)
    g.server:exec(function()
        box.space.test:truncate()
    end)
end)

g.test_box_checkpoint_wal_threshold_contract = function(g)
    define_helpers(g.server)
    -- let's test our assumptions about wal size compared
    -- to tuples and manual_gc_trigger first:
    -- 1. tupls_per_batch tuples produce log file so its
    --    length 1/2 <= xlog_size / wal_max_size < 1
    -- 2. max_wal_files * tuples_per_batch + 1 tuples is
    --    not enough for checkpoint
    -- 3. manual_gc_trigger deletes all existing xlogs
    g.server:exec(function(max_wal_files, tuples_per_batch)
        local wal_max_size = box.cfg.wal_max_size
        -- no checkpoints during the test except explicit ones
        local checkpoint_wal_threshold = box.cfg.checkpoint_wal_threshold
        box.cfg{checkpoint_wal_threshold = 1e18}
        local s = box.space.test
        s:insert{box.NULL}
        local trigger_gc =_G.manual_gc_trigger
        trigger_gc()
        local xlog_total_size = _G.measure_xlog_size_sum
        t.assert_equals(xlog_total_size(), 0)
        for _=1,tuples_per_batch do
            s:insert{box.NULL}
        end
        local xlog_size_after = xlog_total_size()
        t.assert_lt(xlog_size_after,  wal_max_size)
        t.assert_ge(2 * xlog_size_after, wal_max_size)
        -- return the previous configuration
        box.cfg{checkpoint_wal_threshold = checkpoint_wal_threshold}
        trigger_gc()
        local signature_before = box.info.signature
        -- no checkpoint should be created with these tuples alone
        for _=1,max_wal_files * tuples_per_batch + 1 do
            s:insert{box.NULL}
        end
        -- wait for possible (but unwanted) checkpoint finish
        require("fiber").sleep(1)
        t.assert_equals(box.info.gc().checkpoints[1].signature,
                        signature_before)
    end, {max_wal_files, tuples_per_batch})
end

g.test_box_checkpoint_wal_threshold_after_restart = function(g)
    define_helpers(g.server)
    g.server:exec(function(tuples_per_batch, max_wal_files)
        local s = box.space.test
        _G.manual_gc_trigger()
        local wal_max_size = box.cfg.wal_max_size
        for _=1,max_wal_files * tuples_per_batch do
            -- should not trigger checkpointing
            s:insert{box.NULL}
        end
        local xlog_total_size = _G.measure_xlog_size_sum
        t.assert_ge(2 * xlog_total_size(), max_wal_files * wal_max_size)
    end, {tuples_per_batch, max_wal_files})
    g.server:restart()
    define_helpers(g.server)
    g.server:exec(function(tuples_per_batch, max_wal_files)
        local signature_before = box.info.signature
        local s = box.space.test
        -- checkpoint must be created after that
        for _=1,max_wal_files * tuples_per_batch + 1 do
            s:insert{box.NULL}
        end
        -- wait for checkpoint finish
        require("fiber").sleep(1)
        t.assert_gt(box.info.gc().checkpoints[1].signature, signature_before)
    end, {tuples_per_batch, max_wal_files})
end
