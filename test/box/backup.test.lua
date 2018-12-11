fio = require 'fio'
log = require 'log'
test_run = require('test_run').new()

test_run:cleanup_cluster()

-- Basic argument check.
box.backup.start('test')
box.backup.start(-1)

default_checkpoint_count = box.cfg.checkpoint_count

ENGINES = {'memtx', 'vinyl'}
BACKUP_DIR = fio.pathjoin(fio.cwd(), 'backup')

-- Make test spaces.
for _, engine in ipairs(ENGINES) do box.schema.space.create(engine, {engine=engine}):create_index('pk') end

_ = test_run:cmd("setopt delimiter ';'")
-- Function that updates content of test spaces.
function do_update(files)
    for _, engine in ipairs(ENGINES) do
        local s = box.space[engine]
        for i = 1, 3 do
            s:upsert({i, engine .. i, 1}, {{'+', 3, 1}})
        end
    end
end;
-- Function that copies files returned by box.backup.start to BACKUP_DIR.
function do_backup(files)
    os.execute(string.format('rm -rf %s', BACKUP_DIR))
    log.info(string.format('save backup to %s', BACKUP_DIR))
    for _, path in ipairs(files) do
        local dir
        local suffix = string.gsub(path, '.*%.', '')
        if suffix == 'xlog' then
            dir = box.cfg.wal_dir
        elseif suffix == 'snap' then
            dir = box.cfg.memtx_dir
        elseif suffix == 'vylog' or suffix == 'run' or suffix == 'index' then
            dir = box.cfg.vinyl_dir
        end
        assert(dir ~= nil)
        local rel_path = string.sub(path, string.len(dir) + 2)
        local dest_dir = fio.pathjoin(BACKUP_DIR, fio.dirname(rel_path))
        log.info(string.format('copy %s', rel_path))
        os.execute(string.format('mkdir -p %s && cp %s %s', dest_dir, path, dest_dir))
    end
end;
_ = test_run:cmd("setopt delimiter ''");

-- Make a checkpoint to backup.
do_update()
box.snapshot()

-- Add more data, but don't make a checkpoint.
-- These data won't make it to the backup.
do_update()

-- Start backup.
files = box.backup.start()
box.backup.start() -- error: backup is already in progress

-- Even though checkpoint_count is set to 1, this must not trigger
-- garbage collection, because the checkpoint is pinned by backup.
box.cfg{checkpoint_count = 1}
box.snapshot()

-- Copy files.
do_backup(files)
box.backup.stop()

-- Wait for the garbage collector to remove the previous checkpoint.
test_run:wait_cond(function() return #box.info.gc().checkpoints == 1 end, 10)
files = box.backup.start(1) -- error: checkpoint not found

-- Check that we can restore from the backup we've just made.
_ = test_run:cmd(string.format("create server copy1 with script='box/backup_test.lua', workdir='%s'", BACKUP_DIR))
_ = test_run:cmd("start server copy1")
_ = test_run:cmd('switch copy1')
box.space.memtx:select()
box.space.vinyl:select()
_ = test_run:cmd('switch default')
_ = test_run:cmd("stop server copy1")
_ = test_run:cmd("cleanup server copy1")

-- Make another checkpoint. Do not delete the previous one.
box.cfg{checkpoint_count = 2}
do_update()
box.snapshot()

-- Backup and restore the previous checkpoint.
files = box.backup.start(1)
do_backup(files)
box.backup.stop()

_ = test_run:cmd(string.format("create server copy2 with script='box/backup_test.lua', workdir='%s'", BACKUP_DIR))
_ = test_run:cmd("start server copy2")
_ = test_run:cmd('switch copy2')
box.space.memtx:select()
box.space.vinyl:select()
_ = test_run:cmd('switch default')
_ = test_run:cmd("stop server copy2")
_ = test_run:cmd("cleanup server copy2")

-- Backup and restore the last checkpoint. Pass its index explicitly.
files = box.backup.start(0)
do_backup(files)
box.backup.stop()

_ = test_run:cmd(string.format("create server copy3 with script='box/backup_test.lua', workdir='%s'", BACKUP_DIR))
_ = test_run:cmd("start server copy3")
_ = test_run:cmd('switch copy3')
box.space.memtx:select()
box.space.vinyl:select()
_ = test_run:cmd('switch default')
_ = test_run:cmd("stop server copy3")
_ = test_run:cmd("cleanup server copy3")

-- Cleanup.
_ =  os.execute(string.format('rm -rf %s', BACKUP_DIR))
for _, engine in ipairs(ENGINES) do box.space[engine]:drop() end

box.cfg{checkpoint_count = default_checkpoint_count}
