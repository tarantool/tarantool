test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')

--
-- gh-6554: GC removes xlog.inprogress files.
--
assert(box.cfg.wal_dir == box.cfg.memtx_dir)

function count_inprogress() \
    return #fio.glob(box.cfg.wal_dir .. '/*.xlog.inprogress') \
end

-- Run GC after each checkpoint.
checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}

s = box.schema.create_space('test')
_ = s:create_index('pk')
box.snapshot()

-- Suspend GC.
files = box.backup.start()

-- Create a checkpoint.
_ = s:replace{1}
box.snapshot()

-- Block a writer on xlog.inprogress -> xlog rename.
box.error.injection.set('ERRINJ_XLOG_RENAME_DELAY', true)
c = fiber.channel()
_ = fiber.create(function() local r = pcall(s.replace, s, {1}) c:put(r) end)
_ = test_run:wait_cond(function() return count_inprogress() > 0 end)
assert(count_inprogress() == 1)

-- Resume GC and wait for it to delete old files.
box.backup.stop()
for _, f in ipairs(files) do \
    test_run:wait_cond(function() \
        return not fio.path.exists(f) \
    end) \
end

-- The xlog.inprogress file shouldn't be deleted by GC.
assert(count_inprogress() == 1)

-- Resume the blocked writer and check that it succeeds.
box.error.injection.set('ERRINJ_XLOG_RENAME_DELAY', false)
assert(c:get() == true)

-- The xlog.inprogress file was renamed.
assert(count_inprogress() == 0)

-- Cleanup.
s:drop()
box.cfg{checkpoint_count = checkpoint_count}
