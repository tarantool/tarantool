#include "unit.h"
#include "uuid/tt_uuid.h"
#include <string.h>

static void
uuid_test(struct tt_uuid a, struct tt_uuid b, int expected_result)
{
        char a_str[UUID_STR_LEN + 1];
        char b_str[UUID_STR_LEN + 1];

        tt_uuid_to_string(&a, a_str);
        tt_uuid_to_string(&b, b_str);

        int cmp_result = tt_uuid_compare(&a, &b);

        char *sign = 0;

        if (cmp_result == 1)
                sign = ">";
        else if (cmp_result == -1)
                sign = "<";
        else
                sign = "=";

        is(cmp_result,
           expected_result,
           "%s %s %s", a_str, sign, b_str);
}

int
main(void)
{
        plan(2);

        uuid_test(
                (struct tt_uuid){.time_low = 1712399963,
                                .time_mid = 34898,
                                .time_hi_and_version = 18482,
                                .clock_seq_hi_and_reserved = 175,
                                .clock_seq_low = 139,
                                .node = "Ad\325,b\353"},
                (struct tt_uuid){.time_low = 409910263,
                                .time_mid = 53143,
                                .time_hi_and_version = 20014,
                                .clock_seq_hi_and_reserved = 139,
                                .clock_seq_low = 27,
                                .node = "v\025Oo9I"},
                1);


        uuid_test(
                (struct tt_uuid){.time_low = 123421000,
                                .time_mid = 36784,
                                .time_hi_and_version = 11903,
                                .clock_seq_hi_and_reserved = 175,
                                .clock_seq_low = 80,
                                .node = "Ad\325,b\353"},
                (struct tt_uuid){.time_low = 532451999,
                                .time_mid = 23976,
                                .time_hi_and_version = 10437,
                                .clock_seq_hi_and_reserved = 139,
                                .clock_seq_low = 54,
                                .node = "v\025Oo9I"},
                -1);

        return check_plan();
}
