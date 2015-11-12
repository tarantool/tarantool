import sys
import os

# mask BFD warnings: https://bugs.launchpad.net/tarantool/+bug/1018356
sys.stdout.push_filter("unable to read unknown load command 0x2\d+", "")
server.test_option("--help")
server.test_option("-h")
sys.stdout.push_filter("(/\S+)+/tarantool", "tarantool")
server.test_option("-Z")
server.test_option("--no-such-option")
server.test_option("--version --no-such-option")
sys.stdout.push_filter("(\d)\.\d\.\d(-\d+-\w+)?", "\\1.minor.patch-<rev>-<commit>")
sys.stdout.push_filter("Target: .*", "Target: platform <build>")
sys.stdout.push_filter(".*Disable shared arena since.*\n", "")
sys.stdout.push_filter("Build options: .*", "Build options: flags")
sys.stdout.push_filter("C_FLAGS:.*", "C_FLAGS: flags")
sys.stdout.push_filter("CXX_FLAGS:.*", "CXX_FLAGS: flags")
sys.stdout.push_filter("Compiler: .*", "Compiler: cc")

server.test_option("--version")
server.test_option("-V          ")
sys.stdout.clear_all_filters()

# Args filter cleanup
# vim: syntax=python
