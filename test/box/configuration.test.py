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

# restore default server
server.stop()
shutil.rmtree(script_dir_path, True)
server.deploy(self.suite_ini["config"])

sys.stdout.pop_filter()
