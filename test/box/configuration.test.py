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
server.deploy("box/tarantool_bug876541.cfg")
# check values
admin("box.cfg.wal_fsync_delay")

script_dir_path = os.path.join(vardir, "script_dir")
os.mkdir(script_dir_path)
shutil.copy("box/test_init.lua", os.path.join(script_dir_path, "init.lua"))

server.stop()
server.deploy("box/tarantool_scriptdir.cfg")
admin("print_config()")

# Test script_dir + require
server.stop()
shutil.copy("box/require_init.lua", os.path.join(script_dir_path, "init.lua"))
shutil.copy("box/require_mod.lua", os.path.join(script_dir_path, "mod.lua"))
server.deploy("box/tarantool_scriptdir.cfg")
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
try:
    server.deploy("box/tarantool_bug_gh-99.cfg")
except OSError as e:
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
server.stop()
shutil.rmtree(script_dir_path, True)
server.deploy(self.suite_ini["config"])

