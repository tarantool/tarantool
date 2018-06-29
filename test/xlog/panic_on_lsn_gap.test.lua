--
-- we actually need to know what xlogs the server creates,
-- so start from a clean state
--
--
-- Check how the server is able to find the next
-- xlog if there are failed writes (lsn gaps).
--
env = require('test_run')
test_run = env.new()
test_run:cmd("create server panic with script='xlog/panic.lua'")
test_run:cmd("start server panic")
test_run:cmd("switch panic")
box.info.vclock
s = box.space._schema
-- we need to have at least one record in the
-- xlog otherwise the server believes that there
-- is an lsn gap during recovery.
--
s:replace{"key", 'test 1'}
box.info.vclock
box.error.injection.set("ERRINJ_WAL_WRITE", true)
t = {}
--
-- Try to insert rows, so that it's time to
-- switch WALs. No switch will happen though,
-- since no writes were made.
--
test_run:cmd("setopt delimiter ';'")
for i=1,box.cfg.rows_per_wal do
    status, msg = pcall(s.replace, s, {"key"})
    table.insert(t, msg)
end;
test_run:cmd("setopt delimiter ''");
t
--
-- Before restart: our LSN is 1, because
-- LSN is promoted in tx only on successful
-- WAL write.
--
name = string.match(arg[0], "([^,]+)%.lua")
box.info.vclock
require('fio').glob(name .. "/*.xlog")
test_run:cmd("restart server panic")
--
-- After restart: our LSN is the LSN of the
-- last empty WAL created on shutdown, i.e. 11.
--
box.info.vclock
box.space._schema:select{'key'}
box.error.injection.set("ERRINJ_WAL_WRITE", true)
t = {}
s = box.space._schema
--
-- now do the same
--
test_run:cmd("setopt delimiter ';'")
for i=1,box.cfg.rows_per_wal do
    status, msg = pcall(s.replace, s, {"key"})
    table.insert(t, msg)
end;
test_run:cmd("setopt delimiter ''");
t
box.info.vclock
box.error.injection.set("ERRINJ_WAL_WRITE", false)
--
-- Write a good row after a series of failed
-- rows. There is a gap in LSN, correct,
-- but it's *inside* a single WAL, so doesn't
-- affect WAL search in recover_remaining_wals()
--
s:replace{'key', 'test 2'}
--
-- notice that vclock before and after
-- server stop is the same -- because it's
-- recorded in the last row
--
box.info.vclock
test_run:cmd("restart server panic")
box.info.vclock
box.space._schema:select{'key'}
-- list all the logs
name = string.match(arg[0], "([^,]+)%.lua")
require('fio').glob(name .. "/*.xlog")
-- now insert 10 rows - so that the next
-- row will need to switch the WAL
test_run:cmd("setopt delimiter ';'")
for i=1,box.cfg.rows_per_wal do
    box.space._schema:replace{"key", 'test 3'}
end;
test_run:cmd("setopt delimiter ''");
-- the next insert should switch xlog, but aha - it fails
-- a new xlog file is created but has 0 rows
require('fio').glob(name .. "/*.xlog")
box.error.injection.set("ERRINJ_WAL_WRITE", true)
box.space._schema:replace{"key", 'test 3'}
box.info.vclock
require('fio').glob(name .. "/*.xlog")
-- and the next one (just to be sure
box.space._schema:replace{"key", 'test 3'}
box.info.vclock
require('fio').glob(name .. "/*.xlog")
box.error.injection.set("ERRINJ_WAL_WRITE", false)
-- then a success
box.space._schema:replace{"key", 'test 4'}
box.info.vclock
require('fio').glob(name .. "/*.xlog")
-- restart is ok
test_run:cmd("restart server panic")
box.space._schema:select{'key'}
--
-- Check that if there's an LSN gap between two WALs
-- that appeared due to a disk error and no files is
-- actually missing, we won't panic on recovery.
--
box.space._schema:replace{'key', 'test 4'} -- creates new WAL
box.error.injection.set("ERRINJ_WAL_WRITE_DISK", true)
box.space._schema:replace{'key', 'test 5'} -- fails, makes gap
box.snapshot() -- fails, rotates WAL
box.error.injection.set("ERRINJ_WAL_WRITE_DISK", false)
box.space._schema:replace{'key', 'test 5'} -- creates new WAL
box.error.injection.set("ERRINJ_WAL_WRITE_DISK", true)
box.space._schema:replace{'key', 'test 6'} -- fails, makes gap
box.snapshot() -- fails, rotates WAL
box.space._schema:replace{'key', 'test 6'} -- fails, creates empty WAL
name = string.match(arg[0], "([^,]+)%.lua")
require('fio').glob(name .. "/*.xlog")
test_run:cmd("restart server panic")
box.space._schema:select{'key'}
-- Check that we don't create a WAL in the gap between the last two.
box.space._schema:replace{'key', 'test 6'}
name = string.match(arg[0], "([^,]+)%.lua")
require('fio').glob(name .. "/*.xlog")
test_run:cmd('switch default')
test_run:cmd("stop server panic")
test_run:cmd("cleanup server panic")
