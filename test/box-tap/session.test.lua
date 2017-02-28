#!/usr/bin/env tarantool

--
-- Check that Tarantool creates ADMIN session for #! script
--
box.cfg{log="tarantool.log", memtx_memory=104857600}
print('session.id()', box.session.id())
print('session.uid()', box.session.uid())
os.exit(0)
