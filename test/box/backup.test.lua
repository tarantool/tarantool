fio = require 'fio'
log = require 'log'
test_run = require('test_run').new()

test_run:cleanup_cluster()

-- Make sure that garbage collection is disabled
-- while backup is in progress.
default_checkpoint_count = box.cfg.checkpoint_count
box.cfg{checkpoint_count = 1}

ENGINES = {'memtx', 'vinyl'}

-- Directories where files can be stored,
-- from longest to shortest.
CFG_DIRS = {box.cfg.wal_dir, box.cfg.memtx_dir, box.cfg.vinyl_dir}
table.sort(CFG_DIRS, function(a, b) return #a > #b end)

-- Create and populate tables. Make a snapshot to backup.
_ = test_run:cmd("setopt delimiter ';'")
for _, engine in ipairs(ENGINES) do
    s = box.schema.space.create(engine, {engine=engine})
    _ = s:create_index('pk')
    for i=1,3 do s:insert{i, engine..i} end
end
box.snapshot()
_ = test_run:cmd("setopt delimiter ''");

-- Add more data, but don't make a snapshot.
-- These data won't make it to the backup.
_ = test_run:cmd("setopt delimiter ';'")
for _, engine in ipairs(ENGINES) do
    s = box.space[engine]
    for i=1,3 do s:insert{i*10} end
end
_ = test_run:cmd("setopt delimiter ''");

-- Start backup.
files = box.backup.start()

box.backup.start() -- error: backup is already in progress

-- Make sure new snapshots are not included into an ongoing backups.
_ = test_run:cmd("setopt delimiter ';'")
for _, engine in ipairs(ENGINES) do
    s = box.space[engine]
    for i=1,3 do s:insert{i*100} end
end
-- Even though checkpoint_count is set to 1, this must not trigger
-- garbage collection, because the checkpoint is pinned by backup.
box.snapshot()
_ = test_run:cmd("setopt delimiter ''");

-- Prepare backup directory
backup_dir = fio.pathjoin(fio.cwd(), 'backup')
_ = os.execute(string.format('rm -rf %s', backup_dir))
log.info(string.format('save backup to %s', backup_dir))

-- Copy files to the backup directory
_ = test_run:cmd("setopt delimiter ';'")
for _, path in ipairs(files) do
    suffix = string.gsub(path, '.*%.', '')
    if suffix == 'xlog' then
        dir = box.cfg.wal_dir
    elseif suffix == 'snap' then
        dir = box.cfg.memtx_dir
    elseif suffix == 'vylog' or suffix == 'run' or suffix == 'index' then
        dir = box.cfg.vinyl_dir
    end
    assert(dir ~= nil)
    rel_path = string.sub(path, string.len(dir) + 2)
    dest_dir = fio.pathjoin(backup_dir, fio.dirname(rel_path))
    log.info(string.format('copy %s', rel_path))
    os.execute(string.format('mkdir -p %s && cp %s %s', dest_dir, path, dest_dir))
end
_ = test_run:cmd("setopt delimiter ''");

box.backup.stop()

-- Check that we can restore from the backup.
_ = test_run:cmd(string.format("create server copy with script='box/backup_test.lua', workdir='%s'", backup_dir))
_ = test_run:cmd("start server copy")
_ = test_run:cmd('switch copy')
box.space['memtx']:select()
box.space['vinyl']:select()
_ = test_run:cmd('switch default')
_ = test_run:cmd("stop server copy")
_ = test_run:cmd("cleanup server copy")

-- Check that backup still works.
_ = box.backup.start()
box.backup.stop()

-- Cleanup.
_ =  os.execute(string.format('rm -rf %s', backup_dir))
for _, engine in ipairs(ENGINES) do box.space[engine]:drop() end

box.cfg{checkpoint_count = default_checkpoint_count}
