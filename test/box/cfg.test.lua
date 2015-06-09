--# push filter 'listen: .*' to 'primary: <uri>'
--# push filter 'admin: .*' to 'admin: <uri>'
box.cfg.nosuchoption = 1
cfg_filter(box.cfg)
-- must be read-only
box.cfg()
cfg_filter(box.cfg)

-- check that cfg with unexpected parameter fails.
box.cfg{sherlock = 'holmes'}

-- check that cfg with unexpected type of parameter failes
box.cfg{listen = {}}
box.cfg{wal_dir = 0}
box.cfg{coredump = 'true'}


--------------------------------------------------------------------------------
-- Test of hierarchical cfg type check
--------------------------------------------------------------------------------

box.cfg{slab_alloc_arena = "100500"}
box.cfg{sophia = "sophia"}
box.cfg{sophia = {threads = "threads"}}


--------------------------------------------------------------------------------
-- Test of default cfg options
--------------------------------------------------------------------------------

--# create server cfg_tester1 with script = "box/lua/cfg_test1.lua"
--# start server cfg_tester1
--# create connection cfg_tester_con to cfg_tester1
--# set connection cfg_tester_con
box.cfg.slab_alloc_arena, box.cfg.slab_alloc_factor, box.cfg.sophia.threads, box.cfg.sophia.page_size
--# set connection default
--# drop connection cfg_tester_con
--# stop server cfg_tester1
--# cleanup server cfg_tester1

--# create server cfg_tester2 with script = "box/lua/cfg_test2.lua"
--# start server cfg_tester2
--# create connection cfg_tester_con to cfg_tester2
--# set connection cfg_tester_con
box.cfg.slab_alloc_arena, box.cfg.slab_alloc_factor, box.cfg.sophia.threads, box.cfg.sophia.page_size
--# set connection default
--# drop connection cfg_tester_con
--# stop server cfg_tester2
--# cleanup server cfg_tester2

--# create server cfg_tester3 with script = "box/lua/cfg_test3.lua"
--# start server cfg_tester3
--# create connection cfg_tester_con to cfg_tester3
--# set connection cfg_tester_con
box.cfg.slab_alloc_arena, box.cfg.slab_alloc_factor, box.cfg.sophia.threads, box.cfg.sophia.page_size
--# set connection default
--# drop connection cfg_tester_con
--# stop server cfg_tester3
--# cleanup server cfg_tester3

--# create server cfg_tester4 with script = "box/lua/cfg_test4.lua"
--# start server cfg_tester4
--# create connection cfg_tester_con to cfg_tester4
--# set connection cfg_tester_con
box.cfg.slab_alloc_arena, box.cfg.slab_alloc_factor, box.cfg.sophia.threads, box.cfg.sophia.page_size
--# set connection default
--# drop connection cfg_tester_con
--# stop server cfg_tester4
--# cleanup server cfg_tester4

--# clear filter
