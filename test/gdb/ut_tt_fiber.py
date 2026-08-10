"""
To run tests, just put 'source <path-to-this-file>' in gdb.
"""
import functools
import re
import unittest

import gdb


def gdb_exec(cmd):
    return gdb.execute(cmd, False, True)


def repeat_for_every_fiber_id(func):
    @functools.wraps(func)
    def wrapper(self, *args, **kwargs):
        fiber_rows = gdb_exec("info tt-fibers").splitlines()[1:]
        fiber_row_regex = re.compile(r"(\*?)\s+(?P<fid>\d+)")
        results = []
        for fiber_row in fiber_rows:
            match = fiber_row_regex.match(fiber_row)
            self.assertIsNotNone(match)
            fid = int(match.group("fid"))
            results.append(func(self, fid, *args, **kwargs))
        return results
    return wrapper


def with_fiber_switch(func):
    @functools.wraps(func)
    def wrapper(self, fid, *args, **kwargs):
        gdb_exec("tt-fiber {}".format(fid))
        return func(self, fid, *args, **kwargs)
    return wrapper


def repeat_for_every_fiber(func):
    return repeat_for_every_fiber_id(with_fiber_switch(func))


class TtFiberTestBase(unittest.TestCase):
    __fid = None
    __fid_regex = re.compile(r"Current fiber is (?P<fid>\d+)")

    @classmethod
    def setUpClass(cls):
        cls.__fid = int(cls.__fid_regex.match(gdb_exec("tt-fiber")).group("fid"))

    @classmethod
    def tearDownClass(cls):
        gdb.execute("tt-fiber {}".format(cls.__fid))
        gdb_exec("tt-fiber {}".format(cls.__fid))

    __fiber_info_regex = re.compile(r"(?P<run_marker>\*?)\s+(?P<fid>\d+)\s+(\w+)\s+\"(.+[^\\])\"\s+(?P<unw_marker>\*?)")

    @classmethod
    def fiber_info_match(cls, string):
        return cls.__fiber_info_regex.match(string)


class InfoTtFibersTest(TtFiberTestBase):
    @repeat_for_every_fiber
    def check_info_tt_fibers(self, fid):
        out = gdb_exec("info tt-fibers")

        all_fids = []
        running_fids = []
        unwinding_fids = []

        fiber_rows = out.splitlines()[1:]
        for fiber_row in fiber_rows:
            match = self.fiber_info_match(fiber_row)
            self.assertIsNotNone(match)

            row_fid = int(match.group("fid"))
            row_run_marker = match.group("run_marker")
            row_unw_marker = match.group("unw_marker")

            all_fids.append(row_fid)

            if row_run_marker == "*":
                running_fids.append(row_fid)
            else:
                self.assertEqual(row_run_marker, "")

            if row_unw_marker == "*":
                unwinding_fids.append(fid)
            else:
                self.assertEqual(row_unw_marker, "")

        # Check all fiber ids are unique.
        self.assertEqual(len(set(all_fids)), len(all_fids))
        # Check running fiber marker exists and is the only one.
        self.assertEqual(len(running_fids), 1)
        # Check unwinding fiber marker exists and is the only one.
        self.assertEqual(len(unwinding_fids), 1)
        self.assertEqual(unwinding_fids[0], fid)

        # This return values is a kind of invariant that must be the same for all the fibers.
        # To be checked by the caller.
        return all_fids, running_fids[0]

    def test_info_tt_fibers(self):
        results = self.check_info_tt_fibers()
        # Check that the results are the same for all the fibers.
        self.assertTrue(all(x == results[0] for x in results))


class TtFiberTest(TtFiberTestBase):
    @repeat_for_every_fiber_id
    def test_tt_fiber_switch(self, fid):
        out = gdb_exec("tt-fiber {}".format(fid))
        self.assertTrue("Switching stack unwinder to fiber {}".format(fid) in out)

        fiber_rows = gdb_exec("info tt-fibers").splitlines()[1:]
        unwinding_fibers = []
        for fiber_row in fiber_rows:
            match = self.fiber_info_match(fiber_row)
            self.assertIsNotNone(match, fiber_row)
            if match.group("unw_marker") == "*":
                unwinding_fibers.append(match.group("fid"))

        self.assertEqual(len(unwinding_fibers), 1)
        self.assertEqual(int(unwinding_fibers[0]), fid)

    @repeat_for_every_fiber
    def test_tt_fiber_no_arg(self, fid):
        fibers_info = gdb_exec("info tt-fibers")
        out = gdb_exec("tt-fiber")
        self.assertTrue("Current fiber is {}".format(fid) in out)
        self.assertEqual(fibers_info, gdb_exec("info tt-fibers"))

    @repeat_for_every_fiber
    def test_tt_fiber_with_invalid_fid(self, fid):
        invalid_fid = "not-a-number"
        exp_err = "Invalid fiber ID: {}".format(invalid_fid)
        self.assertRaisesRegex(gdb.error, exp_err, gdb_exec, "tt-fiber {}".format(invalid_fid))

    @repeat_for_every_fiber
    def test_tt_fiber_with_unknown_fid(self, fid):
        unknown_fid = 0xffffffff
        exp_err = "Unknown fiber {}.".format(unknown_fid)
        self.assertRaisesRegex(gdb.error, exp_err, gdb_exec, "tt-fiber {}".format(unknown_fid))


class SimpleTest(unittest.TestCase):
    def test_simple(self):
        self.assertTrue(True)
        # self.assertTrue(False)


# unittest.main(exit=False, defaultTest='InfoTtFibersTest', verbosity=2)
# unittest.main(exit=False, defaultTest='TtFiberTest', verbosity=2)
# unittest.main(exit=False, verbosity=2)
# result = unittest.main(exit=False, defaultTest='SimpleTest', verbosity=2).result
# result = unittest.main(verbosity=2, exit=False).result
# # print(f"unittest.main result={result}")
# if not result.wasSuccessful():
#     raise sys.exit(1)
