--
-- we actually need to know what xlogs the server creates,
-- so start from a clean state
--
-- 
-- Check how well we handle a failed log write
-- in panic_on_wal_error=false mode
--
--# create server dont_panic with script='xlog/xlog.lua'
--# start server dont_panic
--# set connection dont_panic
--
box.error.injection.set("ERRINJ_WAL_WRITE", true)
box.space._schema:insert{"key"}
--# stop server dont_panic 
--# start server dont_panic 
box.space._schema:insert{"key"}
--# stop server dont_panic 
--# start server dont_panic 
box.space._schema:get{"key"}
box.space._schema:delete{"key"}
-- list all the logs
require('fio').glob("*.xlog")
--# stop server dont_panic 
--# cleanup server dont_panic
