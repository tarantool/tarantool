import sys
import os
import re

# Disabled on OpenBSD due to fail #XXXX.
import platform

if platform.system() == "OpenBSD":
    self.skip = 1

# mask BFD warnings: https://bugs.launchpad.net/tarantool/+bug/1018356
sys.stdout.push_filter("unable to read unknown load command 0x2\d+", "")
sys.stdout.push_filter("Tarantool (\d+)\.\d+\.\d+(-\w+)*",
                       "Tarantool \\1.<minor>.<patch>-<suffix>")
server.test_option("--help")
server.test_option("-h")
# Replace with the same value for case when builddir inside source dir
sys.stdout.push_filter(re.escape(server.binary), "tarantool")
sys.stdout.push_filter(re.escape(os.getenv("BUILDDIR")), "${SOURCEDIR}")
sys.stdout.push_filter(re.escape(os.getenv("SOURCEDIR")), "${SOURCEDIR}")
sys.stdout.push_filter("invalid option.*", "invalid option")
sys.stdout.push_filter("unrecognized option.*", "unrecognized option")
server.test_option("-Z")
server.test_option("--no-such-option")
server.test_option("--no-such-option --version")
sys.stdout.push_filter(".* (\d+)\.\d+\.\d+(-\w+)*",
                       "Tarantool \\1.<minor>.<patch>-<suffix>")
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

# gh-3966: os.exit() hangs if called by a command from the argument list.
server.test_option("-e 'print(1) os.exit() print(2)'")
server.test_option("-e 'print(1)' -e 'os.exit()' -e 'print(1)' -e 'os.exit()' -e 'print(1)'")

server.test_option("-e \"print('Hello')\" " + script + " 1 2 3")
server.test_option("-e 'a = 10' " + \
                   "-e print(a) " + \
                   script + \
                   " 1 2 3 --help")
server.test_option("-e \"print(rawget(_G, 'log') == nil)\" " + \
                   "-e io.flush() " + \
                   "-l log " + \
                   "-e \"print(log.info('Hello'))\" " + \
                   script + \
                   " 1 2 3 --help")

b_cmds = ["-b", "-bl -e ''", "-b -l -e ''", "-b -e '' output"]
for cmd in b_cmds:
    res = server.test_option_get(cmd, silent=True)
    print(cmd)
    print(res, end='')

j_cmds = ["-jon -e ''", "-j on -e ''"]
for cmd in j_cmds:
    res = server.test_option_get(cmd, silent=True)
    assert res == ""
    print(cmd)

sys.stdout.clear_all_filters()
