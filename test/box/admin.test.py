# encoding: utf-8
# 
import sys

# clear statistics:
server.stop()
server.deploy()

admin("box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')")
admin("box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')")

admin("box.stat()")
admin("help()")
admin("box.cfg()")
admin("box.stat()")
sql("insert into t0 values (1, 'tuple')")
admin("box.snapshot()")
sql("delete from t0 where k0 = 1")
sys.stdout.push_filter("(\d)\.\d\.\d(-\d+-\w+)?", "\\1.minor.patch-<rev>-<commit>")
sys.stdout.push_filter("pid: \d+", "pid: <pid>")
sys.stdout.push_filter("logger_pid: \d+", "pid: <pid>")
sys.stdout.push_filter("uptime: \d+", "uptime: <uptime>")
sys.stdout.push_filter("flags: .*", "flags: <flags>")
sys.stdout.push_filter("build: .*", "build: <build>")
sys.stdout.push_filter("options: .*", "options: <options>")
sys.stdout.push_filter("target: .*", "target: <target>")
sys.stdout.push_filter("compiler: .*", "compiler: <compiler>")
sys.stdout.push_filter("^    [^:]+$", "")
sys.stdout.push_filter("(/\S+)+/tarantool", "tarantool")
admin("box.info()")
sys.stdout.clear_all_filters()
sys.stdout.push_filter(".*", "")
admin("box.fiber.info()")
admin("box.slab.info()")

sys.stdout.clear_all_filters()

admin("box.space[0]:drop()")

# vim: syntax=python
