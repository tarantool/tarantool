--
-- we actually need to know what xlogs the server creates,
-- so start from a clean state
--
--
-- Check how the server is able to find the next
-- xlog if there are failed writes (lsn gaps).
--
--# create server panic with script='xlog/panic.lua'
--# start server panic
--# set connection panic
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
--# setopt delimiter ';'
for i=1,box.cfg.rows_per_wal do
    status, msg = pcall(s.replace, s, {"key"})
    table.insert(t, msg)
end;
--# setopt delimiter ''
t
--
-- Before restart: oops, our LSN is 11,
-- even though we didn't insert anything.
--
name = string.match(arg[0], "([^,]+)%.lua")
box.info.vclock
require('fio').glob(name .. "/*.xlog")
--# stop server panic
--# start server panic
--
-- after restart: our LSN is the LSN of the
-- last *written* row, all the failed
-- rows are gone from lsn counter.
--
box.info.vclock
box.space._schema:select{'key'}
box.error.injection.set("ERRINJ_WAL_WRITE", true)
t = {}
s = box.space._schema
--
-- now do the same
--
--# setopt delimiter ';'
for i=1,box.cfg.rows_per_wal do
    status, msg = pcall(s.replace, s, {"key"})
    table.insert(t, msg)
end;
--# setopt delimiter ''
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
--# stop server panic
--# start server panic
box.info.vclock
box.space._schema:select{'key'}
-- list all the logs
name = string.match(arg[0], "([^,]+)%.lua")
require('fio').glob(name .. "/*.xlog")
-- now insert 10 rows - so that the next
-- row will need to switch the WAL
--# setopt delimiter ';'
for i=1,box.cfg.rows_per_wal do
    box.space._schema:replace{"key", 'test 3'}
end;
--# setopt delimiter ''
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
--# stop server panic
--# start server panic
box.space._schema:select{'key'}
--# stop server panic
--# cleanup server panic
--# set connection default
