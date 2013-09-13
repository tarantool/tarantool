import sys

# clear statistics:
server.stop()
server.deploy()

admin("space = box.schema.create_space('tweedledum', { id = 0 })")
admin("space:create_index('primary', 'hash', { parts = { 0, 'num' }})")

admin("box.stat()")
admin("help()")
sys.stdout.push_filter("'function: .*", "function_ptr")
admin("box.cfg()")
sys.stdout.clear_all_filters()
admin("box.stat()")
admin("box.insert(0, 1, 'tuple')")
admin("box.snapshot()")
admin("box.delete(0, 1)")
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

admin("space:drop()")
