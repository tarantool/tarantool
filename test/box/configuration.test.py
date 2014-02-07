import os
import sys
import shutil

# mask BFD warnings: https://bugs.launchpad.net/tarantool/+bug/1018356
sys.stdout.push_filter("unable to read unknown load command 0x2\d+", "")

print """
# Bug #876541:
#  Test floating point values (wal_fsync_delay) with fractional part
#  (https://bugs.launchpad.net/bugs/876541)
"""
# stop current server
server.stop()

cfgfile_bkp = server.cfgfile_source
server.cfgfile_source = "box/tarantool_bug876541.cfg"
server.deploy()
# check values
admin("box.cfg.wal_fsync_delay")

server.stop()
old_binary = server.binary
server.cfgfile_source = "box/tarantool_scriptdir.cfg"
server.shebang = "box/lua/test_init.lua"
server.deploy()
os.chmod(server.init_lua, 0744)
sys.stdout.push_filter("admin_port: .*", "admin_port: <number>")
sys.stdout.push_filter("primary_port: .*", "primary_port: <number>")
admin("print_config()")
sys.stdout.pop_filter()
sys.stdout.pop_filter()

print """
# Test bug #977898
"""
# Run a dummy insert to avoid race conditions under valgrind
admin("box.space.tweedledum:insert{4, 8, 16}")

print """
# Test insert from init.lua
"""
admin("box.space.tweedledum:select{1}")
admin("box.space.tweedledum:select(2)")
admin("box.space.tweedledum:select(4)")

print """
# Test bug #1002272
"""
admin("floor(0.5)")
admin("floor(0.9)")
admin("floor(1.1)")

# Test script_dir + require
server.stop()
server.shebang = "box/lua/require_init.lua"
server.lua_libs.append("box/lua/require_mod.lua")
server.cfgfile_source = "box/tarantool_scriptdir.cfg"
server.deploy()
server.lua_libs.pop()
admin("string.gmatch(package_path, '([^;]*)')()")
admin("string.gmatch(package_cpath, '([^;]*)')()")
admin("mod.test(10, 15)")

sys.stdout.pop_filter()

print """
# Bug#99 Salloc initialization is not checked on startup
#  (https://github.com/tarantool/tarantool/issues/99)
"""
# stop current server
server.stop()

# Restore the old binary
self.shebang=None
try:
    server.cfgfile_source="box/tarantool_bug_gh-99.cfg"
    server.deploy()
except OSError as e:
    print e
    print("ok")

print """
# Bug#100 Segmentation fault if rows_per_wal = 1
#  (https://github.com/tarantool/tarantool/issues/100)
"""
# stop current server
server.stop()
sys.stdout.push_filter("(/\S+)+/tarantool", "tarantool")
server.test_option("-c " + os.path.join(os.getcwd(), "box/tarantool_bug_gh100.cfg"))
sys.stdout.pop_filter()

# restore default server
server.shebang = None
server.cfgfile_source = cfgfile_bkp
server.deploy()
