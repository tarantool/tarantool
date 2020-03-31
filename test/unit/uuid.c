#include "unit.h"
#include "uuid/tt_uuid.h"
#include "uuid/mp_uuid.h"
#include "core/random.h"
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

static void
mp_uuid_test()
{
        plan(4);
        char buf[18];
        char *data = buf;
        const char *data1 = buf;
        struct tt_uuid uu, ret;
        random_init();
        tt_uuid_create(&uu);
        char *end = mp_encode_uuid(data, &uu);
        is(end - data, mp_sizeof_uuid(), "mp_sizeof_uuid() == encoded length");
        struct tt_uuid *rc = mp_decode_uuid(&data1, &ret);
        is(rc, &ret, "mp_decode_uuid() return code");
        is(data1, end, "mp_sizeof_uuid() == decoded length");
        is(tt_uuid_compare(&uu, &ret), 0, "mp_decode_uuid(mp_encode_uuid(uu)) == uu");
        check_plan();
}

int
main(void)
{
        plan(3);

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

        mp_uuid_test();

        return check_plan();
}
