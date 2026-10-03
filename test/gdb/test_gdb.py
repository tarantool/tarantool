import os


def ut_file(test_file):
    return os.path.join(os.path.dirname(__file__), test_file)


def test_tt_fiber_live(tarantool_gdb):
    tarantool_gdb.test_live(ut_file("ut_tt_fiber.py"), "run_script_f", "-e", "print('Hi')")


def test_tt_fiber_postmortem(tarantool_gdb, tarantool_coredump):
    tarantool_gdb.test_postmortem(ut_file("ut_tt_fiber.py"), tarantool_coredump)
