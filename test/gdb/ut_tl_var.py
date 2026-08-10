"""
To run tests, just put 'source <path-to-this-file>' in gdb.
"""
import functools
import unittest

import gdb


if not hasattr(unittest.TestCase, 'assertRegex'):
    unittest.TestCase.assertRegex = unittest.TestCase.assertRegexpMatches


def repeat_for_every_thread(func):
    @functools.wraps(func)
    def wrapper(*args, **kwargs):
        results = []
        for th in gdb.selected_inferior().threads():
            th.switch()
            results.append(func(*args, **kwargs))
        return results if any(x is not None for x in results) else None
    return wrapper


def log(msg):
    gdb.write(msg + "\n")
    pass


class TlVarTestBase(unittest.TestCase):
    __orig_thread = None

    @classmethod
    def setUpClass(cls):
        cls.__orig_thread = gdb.selected_thread()

    @classmethod
    def tearDownClass(cls):
        cls.__orig_thread.switch()

    @staticmethod
    def tl_var_addr(offset):
        return gdb.parse_and_eval("$tl_var_addr({})".format(offset))

    @staticmethod
    def tl_var_find_offset(ref_val_expr, search_range=None):
        log("TlVarTestBase.tl_var_find_offset: ref_val_expr={}".format(ref_val_expr))
        log("TlVarTestBase.tl_var_find_offset: search_range={}".format(search_range))
        args = ref_val_expr
        if search_range is not None:
            args += "," + str(search_range)
        log("TlVarTestBase.tl_var_find_offset: args={}".format(args))
        log("TlVarTestBase.tl_var_find_offset: expr={}".format("$tl_var_find_offset({})".format(args)))
        res = gdb.parse_and_eval("$tl_var_find_offset({})".format(args))
        log("TlVarTestBase.tl_var_find_offset: eval done")
        log("TlVarTestBase.tl_var_find_offset: res.type.code={}".format(res.type.code))
        log("TlVarTestBase.tl_var_find_offset: res={}".format(res))
        return gdb.parse_and_eval("$tl_var_find_offset({})".format(args))

    @staticmethod
    def tl_var_like(ref_val_expr, search_range=None):
        args = ref_val_expr
        if search_range is not None:
            args += "," + str(search_range)
        return gdb.parse_and_eval("$tl_var_like({})".format(args))


class TlVarAddrTest(TlVarTestBase):
    @repeat_for_every_thread
    def check_addr(self, ref_val_expr, offs):
        ref_addr = self.tl_var_like(ref_val_expr).address
        addr = self.tl_var_addr(offs)
        self.assertEqual(addr, ref_addr)
        return addr

    def not_test(self):
        ref_val_expr = "&main_cord"
        ref_val = gdb.parse_and_eval(ref_val_expr)
        offset = int(self.tl_var_find_offset(ref_val_expr))

        addrs = self.check_addr(ref_val_expr, offset)

        # Check that all addresses are unique.
        self.assertEqual(len(set(addrs)), len(addrs))

        # Check that one of them points to the reference value.
        self.assertIn(ref_val.address, addrs)


class TlVarFindOffsetTest(TlVarTestBase):
    @repeat_for_every_thread
    def check_offset(self, ref_val_expr, ref_val):
        log("TlVarFindOffsetTest.check_offset: ref_val_expr={}".format(ref_val_expr))
        offset = self.tl_var_find_offset(ref_val_expr)
        log("TlVarFindOffsetTest.check_offset: offset={}".format(offset))
        log("TlVarFindOffsetTest.check_offset: ref_val.type.sizeof={}".format(ref_val.type.sizeof))
        offset = int(offset)
        log("TlVarFindOffsetTest.check_offset: offset={}".format(offset))
        self.assertLessEqual(offset, -ref_val.type.sizeof)
        return offset

    def test_success(self):
        ref_val_expr = "&main_cord"
        ref_val = gdb.parse_and_eval(ref_val_expr)
        results = self.check_offset(ref_val_expr, ref_val)
        # Check that the results are the same for all the threads.
        self.assertTrue(all(x == results[0] for x in results))

    @repeat_for_every_thread
    def test_failed(self):
        ref_val_expr = "&main_cord"
        offset = int(self.tl_var_find_offset(ref_val_expr))
        self.assertLess(offset, -1)
        search_range = -offset - 1
        err_msg = "Failed to find the given value in any TLS."
        self.assertRaisesRegex(gdb.error, err_msg,
            self.tl_var_find_offset, ref_val_expr, search_range)

    @repeat_for_every_thread
    def test_negative_range(self):
        ref_val_expr = "&main_cord"
        search_range = -1
        err_msg = "Invalid range {}. It must be greater than zero.".format(search_range)
        self.assertRaisesRegex(gdb.error, err_msg,
            self.tl_var_find_offset, ref_val_expr, search_range)

    @repeat_for_every_thread
    def test_short_range(self):
        ref_val_expr = "&main_cord"
        ref_val = gdb.parse_and_eval(ref_val_expr)
        search_range = ref_val.type.sizeof - 1
        err_msg = "Range of size {} is not able to contain variable of size {}.".format(
            search_range,
            ref_val.type.sizeof,
        )
        self.assertRaisesRegex(gdb.error, err_msg,
            self.tl_var_find_offset, ref_val_expr, search_range)


class TlVarLikeTest(TlVarTestBase):
    @repeat_for_every_thread
    def check_var_like(self, ref_val_expr, ref_val):
        val = self.tl_var_like(ref_val_expr)
        self.assertEqual(val.type, ref_val.type)
        return val

    def test_success(self):
        ref_val_expr = "&main_cord"
        ref_val = gdb.parse_and_eval(ref_val_expr)
        results = self.check_var_like(ref_val_expr, ref_val)
        # Check that the results are the same for all the threads.
        self.assertTrue(all(x == results[0] for x in results))

    @repeat_for_every_thread
    def test_failed(self):
        ref_val_expr = "&main_cord"
        offset = int(self.tl_var_find_offset(ref_val_expr))
        self.assertLess(offset, -1)
        search_range = -offset - 1
        err_msg = "Failed to find the given value in any TLS."
        self.assertRaisesRegex(gdb.error, err_msg,
            self.tl_var_like, ref_val_expr, search_range)

    @repeat_for_every_thread
    def test_negative_range(self):
        ref_val_expr = "&main_cord"
        search_range = -1
        err_msg = "Invalid range {}. It must be greater than zero.".format(search_range)
        self.assertRaisesRegex(gdb.error, err_msg,
            self.tl_var_like, ref_val_expr, search_range)

    @repeat_for_every_thread
    def test_short_range(self):
        ref_val_expr = "&main_cord"
        ref_val = gdb.parse_and_eval(ref_val_expr)
        search_range = ref_val.type.sizeof - 1
        err_msg = "Range of size {} is not able to contain variable of size {}.".format(
            search_range,
            ref_val.type.sizeof,
        )
        self.assertRaisesRegex(gdb.error, err_msg,
            self.tl_var_like, ref_val_expr, search_range)


# unittest.main(verbosity=2, exit=False)
# unittest.main(verbosity=2, exit=False, defaultTest='TlVarAddrTest')
# unittest.main(verbosity=2, exit=False, defaultTest='TlVarFindOffsetTest')
# unittest.main(verbosity=2, exit=False, defaultTest='TlVarLikeTest')
