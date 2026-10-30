local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    t.tarantool.skip_if_not_debug()
    cg.server = server:new({
        box_cfg = {
            log_level = 'verbose',
            checkpoint_count = 1,
        },
    })
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
        if box.space.test ~= nil then
            box.space.test:drop()
        end
        box.space._schema:delete('test')
    end)
end)

g.test_gc_while_compaction_in_progress = function(cg)
    cg.server:exec(function()
        local fio = require('fio')

        local s = box.schema.space.create('test', {engine = 'vinyl'})
        s:create_index('primary')

        -- Vinyl cleans up its metadata log (vylog) only when a checkpoint
        -- is created and it always keeps the vylog file corresponding to
        -- the previous checkpoint. Besides, a vylog record is removed in
        -- three steps: first, we mark it as dropped; second, we mark it as
        -- unused; finally, we purge it. So to collect all garbage, we have
        -- to create four checkpoints.
        local function run_gc()
            for i = 1, 4 do
                box.space._schema:replace({'test', i})
                box.snapshot()
            end
        end

        -- Block compaction.
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', true)

        s:replace({1, 10})
        box.snapshot()
        s:replace({1, 20})
        box.snapshot()

        -- Trigger compaction and wait for it to start.
        s.index.primary:compact()
        t.helpers.retrying({}, function()
            t.assert_covers(box.stat.vinyl(), {
                scheduler = {tasks_inprogress = 1}
            })
        end)

        -- Drop the space and run garbage collection.
        s:drop()
        run_gc()

        -- Vinyl metadata must not be deleted while compaction is
        -- in progress.
        t.assert_not_equals(fio.glob('*.vylog'), {})

        -- Unblock compaction and wait for it to complete.
        box.error.injection.set('ERRINJ_VY_COMPACTION_DELAY', false)
        t.helpers.retrying({}, function()
            t.assert_covers(box.stat.vinyl(), {
                scheduler = {tasks_inprogress = 0}
            })
        end)

        -- Run garbage collection and check that all remaining vinyl
        -- files have been purged.
        t.assert(fio.path.exists('512'))
        t.assert_not_equals(fio.glob('*.vylog'), {})
        run_gc()
        t.assert_not(fio.path.exists('512'))
        t.assert_equals(fio.glob('*.vylog'), {})
    end)
end
