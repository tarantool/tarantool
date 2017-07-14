#include <stdio.h>
#include <assert.h>

#include <box/sql.h>
#include "box/sql/sqliteInt.h"

#include "unit.h"

#define size(arr) sizeof(arr) / sizeof(int)

void
do_test(const char * label, int sz, int * aOp, int argc, int expect,
        const char * function, int line)
{
        int tmp[argc];
        int i;

        assert(label);
        assert(aOp);
        assert(bitvec_sz > 0);
        assert(aOp_sz > 0);

        memcpy(tmp, aOp, argc * sizeof(int));

        int result = sqlite3BitvecBuiltinTest(sz, aOp);
        if (result == expect) {
                printf("ok - %s\n", label);
        } else {
                printf("not ok - %s\n", label);
                printf("Bitvec test failed - %s\n", label);
                printf("At function %s at line %d\n", function, line);
                printf("Expected value - %d\n", expect);
                printf("Returned value - %d\n", result);
                printf("Args: %d , { ", sz);

                for (i = 0; i < argc; i++) {
                        printf("%d ", tmp[i]);
                }

                printf("} \n");
        }
}

void
test_errors()
{
        int test_1[] = { 5, 1, 1, 1, 0 };
        do_test("bitvec-1.0.1", 400, test_1, 5, 1,
                __FUNCTION__  , __LINE__);
        int test_2[] = { 5, 1, 234, 1, 0 };
        do_test("bitvec-1.0.2", 400, test_2, size(test_2), 234,
                __FUNCTION__ , __LINE__);
}

void
test_various_sizes()
{
        int test_3[] = { 1, 400, 1, 1, 0 };
        do_test("bitvec-1.1", 400, test_3, size(test_3), 0,
                __FUNCTION__ , __LINE__);
        int test_4[] = { 1, 4000, 1, 1, 0};
        do_test("bitvec-1.2", 4000, test_4, size(test_4), 0,
                __FUNCTION__ , __LINE__);
        int test_5[] = { 1, 40000, 1, 1, 0};
        do_test("bitvec-1.3", 40000, test_5, size(test_5), 0,
                __FUNCTION__ , __LINE__);
        int test_6[] = { 1, 400000, 1, 1, 0};
        do_test("bitvec-1.4", 400000, test_6, size(test_6), 0,
                __FUNCTION__ , __LINE__);
}

void
test_larger_increments()
{
        int test_7[] = { 1, 400, 1, 7, 0};
        do_test("bitvec-1.5", 400, test_7, size(test_7), 0,
                __FUNCTION__ , __LINE__);
        int test_8[] = { 1, 4000, 1, 7, 0};
        do_test("bitvec-1.6", 4000, test_8, size(test_8), 0,
                __FUNCTION__ , __LINE__);
        int test_9[] = { 1,  40000, 1, 7, 0};
        do_test("bitvec-1.7", 40000, test_9, size(test_9), 0,
                __FUNCTION__, __LINE__);
        int test_10[] = { 1, 400000, 1, 7, 0};
        do_test("bitvec-1.8", 400000, test_10, size(test_10), 0,
                __FUNCTION__, __LINE__);
}

void
test_clearing_mechanism()
{
        int test_11[] = {1, 400, 1, 1, 2, 400, 1, 1, 0};
        do_test("bitvec-1.9", 400, test_11, size(test_11), 0,
                 __FUNCTION__, __LINE__);
        int test_12[] = {1, 4000, 1, 1, 2, 4000, 1, 1, 0};
        do_test("bitvec-1.10", 4000, test_12, size(test_12), 0,
                __FUNCTION__, __LINE__);
        int test_13[] = {1, 40000, 1, 1, 2, 40000, 1, 1, 0};
        do_test("bitvec-1.11", 40000, test_13, size(test_13), 0,
                __FUNCTION__, __LINE__);
        int test_14[] = {1, 400000, 1, 1, 2, 400000, 1, 1, 0};
        do_test("bitvec-1.12", 400000, test_14, size(test_14), 0,
                __FUNCTION__, __LINE__);
        int test_15[] = {1, 400, 1, 1, 2, 400, 1, 7, 0};
        do_test("bitvec-1.13", 400, test_15, size(test_15), 0,
                __FUNCTION__, __LINE__);
        int test_16[] = {1, 4000, 1, 1, 2, 4000, 1, 7, 0};
        do_test("bitvec-1.14", 4000, test_16, size(test_16), 0,
                __FUNCTION__, __LINE__);
        int test_17[] = {1, 40000, 1, 1, 2, 40000, 1, 7, 0};
        do_test("bitvec-1.15", 40000, test_17, size(test_17), 0,
                __FUNCTION__, __LINE__);
        int test_18[] = {1, 400000, 1, 1, 2, 400000, 1, 7, 0};
        do_test("bitvec-1.16", 400000, test_18, size(test_17), 0,
                __FUNCTION__, __LINE__);
        int test_19[] = {1, 5000, 100000, 1, 2, 400000, 1, 37, 0};
        do_test("bitvec-1.17", 40000, test_19, size(test_18), 0,
                __FUNCTION__, __LINE__);
}

void
test_hashing_collisions()
{
        int start_values[] = { 1, 8, 1 };
        int incr_values[] = { 124, 125, 1 };
        char bitvec[30];
        int i, j;

        for (i = 0; i < 3; i++) {
                for (j = 0; j < 3; j++) {
                        sprintf(bitvec, "bitvec-1.18.%d.%d", i, j);
                        int test_20[] = { 1, 60, i, j, 2, 5000, 1, 1, 0 };
                        do_test(bitvec, 5000, test_20, size(test_20), 0,
                                __FUNCTION__, __LINE__);
                }
        }

        int test_30[] = {1, 17000000, 1, 1, 2, 17000000, 1, 1, 0};
        do_test("bitvec-1.30.big_and_slow", 17000000, test_30, size(test_30), 0,
                __FUNCTION__, __LINE__);
}

void
test_random_subsets()
{
        int test_31[] = {3, 2000, 4, 2000, 0};
        do_test("bitvec-2.1", 4000, test_31, size(test_31), 0,
                __FUNCTION__, __LINE__);

        int test_32[] = {3, 1000, 4, 1000, 3, 1000, 4, 1000, 3, 1000, 4,
                         1000, 3, 1000, 4, 1000, 3, 1000, 4, 1000, 3, 1000, 4, 1000, 0};
        do_test("bitvec-2.2", 4000, test_32, size(test_32), 0,
                __FUNCTION__, __LINE__);

        int test_33[] = {3, 10, 0};
        do_test("bitvec-2.3", 400000, test_33, size(test_33), 0,
                __FUNCTION__, __LINE__);

        int test_34[] = {3, 10, 2, 4000, 1, 1, 0};
        do_test("bitvec-2.4", 4000, test_34, size(test_34), 0,
                __FUNCTION__, __LINE__);

        int test_35[] = {3, 20, 2, 5000, 1, 1, 0};
        do_test("bitvec-2.5", 5000, test_35, size(test_35), 0,
                __FUNCTION__, __LINE__);

        int test_36[] = {3, 60, 2, 50000, 1, 1, 0};
        do_test("bitvec-2.6", 50000, test_36, size(test_36), 0,
                __FUNCTION__, __LINE__);

        int test_37[] = {1, 25, 121, 125, 1, 50, 121, 125, 2, 25, 121, 125, 0};
        do_test("bitvec-2.7", 5000, test_37, size(test_37), 0,
                __FUNCTION__, __LINE__);
}

/*
 * This is malloc tcl test - needs to be converted
 *
 * -- This procedure runs sqlite3BitvecBuiltinTest with argments "n" and
-- "program".  But it also causes a malloc error to occur after the
-- "failcnt"-th malloc.  The result should be "0" if no malloc failure
-- occurs or "-1" if there is a malloc failure.
--
-- MUST_WORK_TEST sqlite3_memdebug_fail func was removed (with test_malloc.c)
if 0>0 then
local function bitvec_malloc_test(label, failcnt, n, program)
--    do_test $label [subst {
--    sqlite3_memdebug_fail $failcnt
--    set x \[sqlite3BitvecBuiltinTest $n [list $program]\]
--    set nFail \[sqlite3_memdebug_fail -1\]
--    if {\$nFail==0} {
--        set ::go 0
--        set x -1
--        }
--        set x
--        }] -1
end

-- Make sure malloc failures are handled sanily.
--
-- ["unset","-nocomplain","n"]
-- ["unset","-nocomplain","go"]
go = 1
X(177, "X!cmd", [=[["save_prng_state"]]=])
for _ in X(0, "X!for", [=[["set n 0","$go","incr n"]]=]) do
    X(180, "X!cmd", [=[["restore_prng_state"]]=])
    bitvec_malloc_test("bitvec-3.1."..n, n, 5000, [[
      3 60 2 5000 1 1 3 60 2 5000 1 1 3 60 2 5000 1 1 0
  ]])
end
go = 1
for _ in X(0, "X!for", [=[["set n 0","$go","incr n"]]=]) do
    X(187, "X!cmd", [=[["restore_prng_state"]]=])
    bitvec_malloc_test("bitvec-3.2."..n, n, 5000, [[
      3 600 2 5000 1 1 3 600 2 5000 1 1 3 600 2 5000 1 1 0
  ]])
end
go = 1
for _ in X(0, "X!for", [=[["set n 1","$go","incr n"]]=]) do
    bitvec_malloc_test("bitvec-3.3."..n, n, 50000, "1 50000 1 1 0")
end
end
*
*/

void
run_tests(void)
{
        header();

        test_errors();
        test_various_sizes();
        test_larger_increments();
        test_clearing_mechanism();
        test_random_subsets();

        footer();
}

int
main(void)
{
        sqlite3MutexInit();
        sqlite3MallocInit();

        run_tests();

        sqlite3MallocEnd();
        sqlite3MutexEnd();

        return 0;
}
