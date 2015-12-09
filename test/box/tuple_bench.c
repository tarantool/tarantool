#include "module.h"
#include "msgpuck/msgpuck.h"
#include <sys/time.h>
double
proctime(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (double) tv.tv_sec + 1e-6 * tv.tv_usec;

}
int
tuple_bench(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	static const char *SPACE_NAME = "tester";
	static const char *INDEX_NAME = "primary";

	uint32_t space_id = box_space_id_by_name(SPACE_NAME, strlen(SPACE_NAME));
	uint32_t index_id = box_index_id_by_name(space_id, INDEX_NAME,
		strlen(INDEX_NAME));

	if (space_id == BOX_ID_NIL || index_id == BOX_ID_NIL) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C,
			"Can't find index %s in space %s",
			INDEX_NAME, SPACE_NAME);
	}
	say_debug("space_id = %u, index_id = %u", space_id, index_id);

	char tuple_buf[4][64];
	char *tuple_end[4] = {tuple_buf[0], tuple_buf[1],
			      tuple_buf[2], tuple_buf[3]};
	const uint64_t test_numbers[4] = {2, 2, 1, 3};
	const char test_strings[4][4] = {"bce", "abb", "abb", "ccd"};
	/* get key types from args, and build test tuples with according types*/
	uint32_t arg_count = mp_decode_array(&args);
	if (arg_count < 1) {
		return box_error_set(__FILE__, __LINE__, ER_PROC_C, "%s",
			"invalid argument count");
	}
	uint32_t n = mp_decode_array(&args);
	uint32_t knum = 0, kstr = 0;
	for (uint32_t k = 0; k < 4; k++) {
		const char *field = args;
		tuple_end[k] = mp_encode_array(tuple_end[k], n);
		for (uint32_t i = 0; i < n; i++, field += 3) {
			if (mp_decode_strl(&field) != 3) {
				say_error("Arguments must be \"STR\" or \"NUM\"");
				return -1;
			}
			if (memcmp(field, "NUM", 3) == 0) {
				tuple_end[k] = mp_encode_uint(tuple_end[k],
						test_numbers[knum]);
				knum = (knum + 1) % 4;
			} else if (memcmp(field, "STR", 3) == 0) {
				tuple_end[k] = mp_encode_str(tuple_end[k],
						test_strings[kstr],
						strlen(test_strings[kstr]));
				kstr = (kstr + 1) % 4;
			} else {
				say_error("Arguments must be \"STR\" or \"NUM\"");
				return -1;
			}
		}
	}

	double t = proctime();
	box_tuple_t *tuple;
	for (int i = 0; i < 80000000; i++) {
		int k = (i  + (i >> 2) + (i >> 5) + 13) & 3;
		box_index_min(space_id, index_id, tuple_buf[k], tuple_end[k], &tuple);
	}
	t = proctime() - t;
	say_info("%lf\n", t);
	return 0;
}
