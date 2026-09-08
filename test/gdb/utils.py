import glob
import re
import resource
import os
import shutil
import subprocess


# Runs the specified command, that should be composed in a way that crashes
# the application then copies the generated coredump file into work_dir
# (if it's not there already) and returns its path.
# The way coredumps are handled in system is configured with
# /proc/sys/kernel/core_pattern file. This function recognizes the following
# patterns:
# - direct file pattern
# - pipe to apport tool
# - pipe to systemd-coredump
def generate_coredump(cmd, work_dir):
    with open("/proc/sys/kernel/core_pattern", "r") as f:
        core_pattern = f.read().strip()

    to_coredump = None
    if not core_pattern.startswith("|"):
        # Turn core pattern into shell wildcard.
        core_wildcard = core_pattern.replace("%%", "%")
        core_wildcard = re.sub("%[cdeEghiIpPstu]", "*", core_wildcard)
        is_abs = os.path.isabs(core_wildcard)
        if not is_abs:
            core_wildcard = os.path.join(work_dir, core_wildcard)

        orig_cores = set(glob.glob(core_wildcard))
        def to_coredump_direct():
            # Find the newly generated core.
            new_cores = set(glob.glob(core_wildcard)) - orig_cores
            assert len(new_cores) == 1
            new_core = next(iter(new_cores))
            return shutil.copy2(new_core, os.path.join(work_dir, "core")) if is_abs else new_core
        to_coredump = to_coredump_direct

    elif re.search(r"apport", core_pattern):
        crash_wildcard = "/var/crash/*.crash"
        orig_crashes = set(glob.glob(crash_wildcard))
        def to_coredump_apport():
            # Find the newly generated crash.
            new_crashes = set(glob.glob(crash_wildcard)) - orig_crashes
            assert len(new_crashes) == 1
            process = subprocess.run(["apport-unpack", next(iter(new_crashes)), work_dir])
            assert process.returncode == 0
            return os.path.join(work_dir, "CoreDump")
        to_coredump = to_coredump_apport

    elif re.search(r"systemd-coredump", core_pattern):
        def to_coredump_systemd():
            core_path = os.path.join(work_dir, "core")
            process = subprocess.run(["coredumpctl", "dump", "--output={}".format(core_path)])
            assert process.returncode == 0
            return core_path
        to_coredump = to_coredump_systemd

    else:
        assert False, "Unexpected core pattern '{}'".format(core_pattern)

    # Setup ulimit -c.
    rlim_core_soft, rlim_core_hard = resource.getrlimit(resource.RLIMIT_CORE)
    if rlim_core_soft != resource.RLIM_INFINITY:
        resource.setrlimit(resource.RLIMIT_CORE, (resource.RLIM_INFINITY, rlim_core_hard))
    # Crash tarantool.
    p = subprocess.run(cmd, cwd=work_dir)
    # Restore ulimit -c.
    resource.setrlimit(resource.RLIMIT_CORE, (rlim_core_soft, rlim_core_hard))
    assert p.returncode != 0
    # assert re.search(r"Segmentation fault", p.stdout)

    return to_coredump()


class GdbSession(object):
    def __init__(self, gdb_bin, tarantool_bin, tarantool_gdb_ext):
        self.__gdb_bin = gdb_bin
        self.__tarantool_bin = tarantool_bin
        self.__tarantool_gdb_ext = tarantool_gdb_ext

    def test_live(self, ut_file, location, *args):
        self.__run_test(ut_file, [
            "break {}".format(location),
            "set startup-with-shell off",
            "run {}".format(" ".join(args)),
        ])

    def test_postmortem(self, ut_file, coredump):
        self.__run_test(ut_file, [
            "core {}".format(coredump),
        ])

    def __run_test(self, ut_file, gdb_cmds):
        cmd = [
            str(self.__gdb_bin),
            str(self.__tarantool_bin),
            "--batch",
        ]
        for gdb_cmd in gdb_cmds:
            cmd.extend(["-ex", gdb_cmd])
        cmd.extend([
            "-ex", "source {}".format(self.__tarantool_gdb_ext),
            "-ex", "source {}".format(ut_file),
            "-ex", "python if not unittest.main(verbosity=2, exit=False).result.wasSuccessful():\n\traise sys.exit(1)",
        ])
        print(f"cmd44444444444={cmd}")
        p = subprocess.run(
            cmd,
            # stdin=subprocess.PIPE,
            # stdout=subprocess.PIPE,
            # stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
        print(f"p.returncode={p.returncode}")
        assert p.returncode == 0
