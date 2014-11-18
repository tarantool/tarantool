#!/usr/bin/env tarantool

--
-- Check that Tarantool creates ADMIN session for #! script
--
box.cfg{logger="tarantool.log", slab_alloc_arena=0.1}
print('session.id()', box.session.id())
print('session.uid()', box.session.uid())
os.exit(0)
