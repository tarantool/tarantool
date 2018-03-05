import sys
import os
import re

# mask BFD warnings: https://bugs.launchpad.net/tarantool/+bug/1018356
sys.stdout.push_filter("unable to read unknown load command 0x2\d+", "")
server.test_option("--help")
server.test_option("-h")
# Replace with the same value for case when builddir inside source dir
sys.stdout.push_filter(re.escape(os.getenv("BUILDDIR")+'/src/tarantool'), "tarantool")
sys.stdout.push_filter(re.escape(os.getenv("BUILDDIR")), "${SOURCEDIR}")
sys.stdout.push_filter(re.escape(os.getenv("SOURCEDIR")+'/src/tarantool'), "tarantool")
sys.stdout.push_filter(re.escape(os.getenv("SOURCEDIR")), "${SOURCEDIR}")
sys.stdout.push_filter("invalid option.*", "invalid option")
sys.stdout.push_filter("unrecognized option.*", "unrecognized option")
server.test_option("-Z")
server.test_option("--no-such-option")
server.test_option("--no-such-option --version")
sys.stdout.push_filter(".* (\d+)\.\d+\.\d(-\d+-\w+)?", "Tarantool \\1.minor.patch-<rev>-<commit>")
sys.stdout.push_filter("Target: .*", "Target: platform <build>")
sys.stdout.push_filter(".*Disable shared arena since.*\n", "")
sys.stdout.push_filter("Build options: .*", "Build options: flags")
sys.stdout.push_filter("C_FLAGS:.*", "C_FLAGS: flags")
sys.stdout.push_filter("CXX_FLAGS:.*", "CXX_FLAGS: flags")
sys.stdout.push_filter("Compiler: .*", "Compiler: cc")

server.test_option("--version")
server.test_option("-v")
server.test_option("-V          ")

script = os.getenv("SOURCEDIR") + "/test/box-py/args.lua"
server.test_option(script)
server.test_option(script + " 1 2 3")
server.test_option(script + " 1 2 3 -V")
server.test_option(script + " -V 1 2 3")
server.test_option(script + " 1 2 3 --help")
server.test_option(script + " --help 1 2 3")
server.test_option("-V " + script + " 1 2 3")

server.test_option("-e \"print('Hello')\" " + script + " 1 2 3")
server.test_option("-e \"a = 10\" " + \
                   "-e print(a) " + \
                   script + \
                   " 1 2 3 --help")
server.test_option("-e \"print(rawget(_G, 'log') == nil)\" " + \
                   "-e io.flush() " + \
                   "-l log " + \
                   "-e \"print(log.info('Hello'))\" " + \
                   script + \
                   " 1 2 3 --help")

sys.stdout.clear_all_filters()
# Args filter cleanup
# vim: syntax=python
