test_run = require('test_run').new()
fiber = require('fiber')
digest = require('digest')

default_threshold = box.cfg.checkpoint_wal_threshold
threshold = 10 * 1024
box.cfg{checkpoint_wal_threshold = threshold}

s = box.schema.space.create('test')
_ = s:create_index('pk')
box.snapshot()

test_run:cmd("setopt delimiter ';'")
function put(size)
    s:auto_increment{digest.urandom(size)}
end;
function wait_checkpoint(signature)
    signature = signature or box.info.signature
    return test_run:wait_cond(function()
        local checkpoints = box.info.gc().checkpoints
        return signature == checkpoints[#checkpoints].signature
    end, 10)
end;
test_run:cmd("setopt delimiter ''");

--
-- Check that checkpointing is triggered automatically once
-- the size of WAL files written since the last checkpoint
-- exceeds box.cfg.checkpoint_wal_threshold (gh-1082).
--
for i = 1, 3 do put(threshold / 3) end
wait_checkpoint()
for i = 1, 5 do put(threshold / 5) end
wait_checkpoint()

--
-- Check that WAL rows written while a checkpoint was created
-- are accounted as written after the checkpoint.
--
box.error.injection.set('ERRINJ_SNAP_COMMIT_DELAY', true)

-- This should trigger checkpointing, which will take quite
-- a while due to the injected delay.
for i = 1, 5 do put(threshold / 5) end
fiber.sleep(0)

-- Remember the future checkpoint signature.
signature = box.info.signature

-- Insert some records while the checkpoint is created.
for i = 1, 4 do put(threshold / 5) end

-- Disable the delay and wait for checkpointing to complete.
box.error.injection.set('ERRINJ_SNAP_COMMIT_DELAY', false)
wait_checkpoint(signature)

-- Check that insertion of one more record triggers another
-- checkpoint, because it sums up with records written while
-- the previous checkpoint was created.
put(threshold / 5)
wait_checkpoint()

box.cfg{checkpoint_wal_threshold = default_threshold}
s:drop()
