# encoding: utf-8
#
admin("box.cfg.too_long_threshold")
# bad1
server.reconfigure("box/tarantool_bad1.cfg")
admin("box.cfg.too_long_threshold")
# good
server.reconfigure("box/tarantool_good.cfg")
admin("box.cfg.too_long_threshold")
admin("box.cfg.snap_io_rate_limit")
admin("box.cfg.io_collect_interval")
# empty
server.reconfigure("box/tarantool_empty.cfg")
admin("box.cfg.too_long_threshold")
admin("box.cfg.snap_io_rate_limit")
server.reconfigure("box/snap_io_rate_limit.cfg")
admin("box.cfg.snap_io_rate_limit")

# no config
server.reconfigure(None)

# cleanup
# restore default
server.reconfigure(self.suite_ini["config"])
admin("box.cfg.too_long_threshold")

print """#
# A test case for http://bugs.launchpad.net/bugs/712447:
# Valgrind reports use of not initialized memory after 'reload
# configuration'
#"""
admin("space = box.schema.create_space('tweedledum', { id = 0 })")
admin("space:create_index('primary', 'hash', { parts = { 0, 'num' }})")
admin("box.insert(0, 1, 'tuple')")
admin("")
admin("box.snapshot()")
server.reconfigure(None)
admin("box.insert(0, 2, 'tuple2')")
admin("box.snapshot()")
server.reconfigure("box/tarantool_good.cfg")
admin("box.insert(0, 3, 'tuple3')")
admin("box.snapshot()")

print """#
# A test case for https://github.com/tarantool/tarantool/issues/112:
# Tarantool crashes with SIGSEGV during reload configuration
#"""
server.reconfigure("box/tarantool_wo_cpt.cfg")
server.reconfigure("box/tarantool_with_cpt.cfg")

# Cleanup
server.reconfigure(self.suite_ini["config"])
admin("box.space[0]:drop()")

# vim: syntax=python
