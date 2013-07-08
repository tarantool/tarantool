# encoding: utf-8
# 
import sys
# clear statistics:
server.stop()
server.deploy()
admin("exit")
admin("show stat")
admin("help")
admin("show configuration")
admin("show stat")
sql("insert into t0 values (1, 'tuple')")
admin("save snapshot")
sql("delete from t0 where k0 = 1")
sys.stdout.push_filter("(\d)\.\d\.\d(-\d+-\w+)?", "\\1.minor.patch-<rev>-<commit>")
sys.stdout.push_filter("pid: \d+", "pid: <pid>")
sys.stdout.push_filter("logger_pid: \d+", "pid: <pid>")
sys.stdout.push_filter("uptime: \d+", "uptime: <uptime>")
sys.stdout.push_filter("uptime: \d+", "uptime: <uptime>")
sys.stdout.push_filter("(/\S+)+/tarantool", "tarantool")
admin("show info")
sys.stdout.clear_all_filters()
sys.stdout.push_filter(".*", "")
admin("show fiber")
admin("show slab")
admin("show palloc")


sys.stdout.clear_all_filters()

# vim: syntax=python
