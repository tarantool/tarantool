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

old_cfgfile = server.cfgfile_source
server.cfgfile_source = "box/tarantool_bug876541.cfg"
server.deploy()
# check values
admin("box.cfg.wal_fsync_delay")

server.stop()
server.cfgfile_source = old_cfgfile
old_script = server.script
server.script = "box/lua/test_init.lua"
server.deploy()
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
admin("box.space.tweedledum:get(1)")
admin("box.space.tweedledum:get(2)")
admin("box.space.tweedledum:get(4)")

print """
# Test bug #1002272
"""
admin("floor(0.5)")
admin("floor(0.9)")
admin("floor(1.1)")
server.stop()
server.script = old_script

# Test script_dir + require
old_script = server.script
server.script = "box/lua/require_init.lua"
server.lua_libs.append("box/lua/require_mod.lua")
server.deploy()
server.lua_libs.pop()
admin("mod.test(10, 15)")
server.stop()
server.script = old_script


print """
# Bug#99 Salloc initialization is not checked on startup
#  (https://github.com/tarantool/tarantool/issues/99)
"""
old_cfgfile = server.cfgfile_source
server.cfgfile_source="box/tarantool_bug_gh-99.cfg"
try:
    server.deploy()
except OSError as e:
    print e
    print("ok")
server.stop()
server.cfgfile_source = old_cfgfile

print """
# Bug#100 Segmentation fault if rows_per_wal = 0
#  (https://github.com/tarantool/tarantool/issues/100)
"""
old_cfgfile = server.cfgfile_source
server.cfgfile_source = "box/tarantool_bug_gh100.cfg"
try:
    server.deploy()
except OSError as e:
    print e
    print("ok")
server.stop()
server.cfgfile_source = old_cfgfile
print """#
# Check that --background  doesn't work if there is no logger
# This is a test case for
# https://bugs.launchpad.net/tarantool/+bug/750658
# "--background neither closes nor redirects stdin/stdout/stderr"
"""
old_cfgfile = server.cfgfile_source
server.cfgfile_source = "box/tarantool_bug750658.cfg"
try:
    server.deploy()
except OSError as e:
    print e
    print("ok")

server.stop()
server.cfgfile_source = old_cfgfile
print """
# A test case for Bug#726778 "Gopt broke wal_dir and snap_dir: they are no
# longer relative to work_dir".
# https://bugs.launchpad.net/tarantool/+bug/726778
# After addition of gopt(), we started to chdir() to the working
# directory after option parsing.
# Verify that this is not the case, and snap_dir and xlog_dir
# can be relative to work_dir.
"""
import shutil
vardir = server.vardir
shutil.rmtree(os.path.join(vardir, "bug726778"), True)
os.mkdir(os.path.join(vardir, "bug726778"))
os.mkdir(os.path.join(vardir, "bug726778/snapshots"))
os.mkdir(os.path.join(vardir, "bug726778/xlogs"))

sys.stdout.push_filter("(/\S+)+/tarantool", "tarantool")
sys.stdout.push_filter(".*(P|p)lugin.*", "")
sys.stdout.push_filter(".*shared.*", "")
old_cfgfile = server.cfgfile_source
old_logfile = server.logfile
# make sure deploy() looks for start message in a correct
# place
server.logfile = os.path.join(vardir, "bug726778/tarantool.log")
server.cfgfile_source = "box/bug726778.cfg"
server.deploy()
sys.stdout.clear_all_filters()
server.stop()
shutil.rmtree(os.path.join(vardir, "bug726778"))
server.cfgfile_source = old_cfgfile
server.logfile = old_logfile

# restore default server
server.deploy()
