#include "tarantool.h"
#include "msgpuck/msgpuck.h"
#include <time.h>
double
proctime(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
	return (double) ts.tv_sec + ts.tv_nsec / 1e9;

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
		return box_error_raise(ER_PROC_C,
			"Can't find index %s in space %s",
			INDEX_NAME, SPACE_NAME);
	}
	say_debug("space_id = %u, index_id = %u", space_id, index_id);

	char tuple_buf[4][64];
	char *tuple_end[4] = {tuple_buf[0], tuple_buf[1], tuple_buf[2], tuple_buf[3]};
	if(1) {
		tuple_end[0] = mp_encode_array(tuple_end[0], 2);
		tuple_end[0] = mp_encode_uint(tuple_end[0], 2);
		tuple_end[0] = mp_encode_str(tuple_end[0], "bce", strlen("abb"));

		tuple_end[1] = mp_encode_array(tuple_end[1], 2);
		tuple_end[1] = mp_encode_uint(tuple_end[1], 2);
		tuple_end[1] = mp_encode_str(tuple_end[1], "abb", strlen("abb"));

		tuple_end[2] = mp_encode_array(tuple_end[2], 2);
		tuple_end[2] = mp_encode_uint(tuple_end[2], 1);
		tuple_end[2] = mp_encode_str(tuple_end[2], "abb", strlen("abb"));

		tuple_end[3] = mp_encode_array(tuple_end[3], 2);
		tuple_end[3] = mp_encode_uint(tuple_end[3], 3);
		tuple_end[3] = mp_encode_str(tuple_end[3], "ccd", strlen("abb"));
	} else if(0){
		tuple_end[0] = mp_encode_array(tuple_end[0], 1);
		tuple_end[0] = mp_encode_uint(tuple_end[0], 2);

		tuple_end[1] = mp_encode_array(tuple_end[1], 1);
		tuple_end[1] = mp_encode_uint(tuple_end[1], 1);

		tuple_end[2] = mp_encode_array(tuple_end[2], 1);
		tuple_end[2] = mp_encode_uint(tuple_end[2], 1);

		tuple_end[3] = mp_encode_array(tuple_end[3], 1);
		tuple_end[3] = mp_encode_uint(tuple_end[3], 3);
	} else {
		tuple_end[0] = mp_encode_array(tuple_end[0], 1);
		tuple_end[0] = mp_encode_str(tuple_end[0], "bce", strlen("abb"));

		tuple_end[1] = mp_encode_array(tuple_end[1], 1);
		tuple_end[1] = mp_encode_str(tuple_end[1], "abb", strlen("abb"));

		tuple_end[2] = mp_encode_array(tuple_end[2], 1);
		tuple_end[2] = mp_encode_str(tuple_end[2], "abb", strlen("abb"));

		tuple_end[3] = mp_encode_array(tuple_end[3], 1);
		tuple_end[3] = mp_encode_str(tuple_end[3], "ccd", strlen("abb"));
	}

	//tuple_cmp = tuple_compare_with_key;
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
