--
-- gh-4775: crash on option concatenated with value.
--
child = io.popen('tarantool -e"print(100) os.exit()"')
child:read()
-- :close() is omitted, because SIGCHILD may be handled by
-- libev instead of Lua. In that case :close() with fail with
-- ECHILD, but it does not matter for this test.
