#include "module.h"
#include "msgpuck.h"

int
index_get(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	assert(arg_count == 3);
	(void)arg_count;
	uint64_t u64 = mp_decode_uint(&args);
	assert(u64 <= UINT32_MAX);
	uint32_t space_id = (uint32_t)u64;
	u64 = mp_decode_uint(&args);
	assert(u64 <= UINT32_MAX);
	uint32_t index_id = (uint32_t)u64;
	box_tuple_t *tuple = NULL;
	int rc = box_index_get(space_id, index_id, args, args_end, &tuple);
	if (rc < 0)
		return -1;
	if (tuple == NULL)
		return 0;
	box_return_tuple(ctx, tuple);
	return 0;
}

int
index_upsert(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	assert(arg_count == 4);
	(void)arg_count;
	uint64_t u64 = mp_decode_uint(&args);
	assert(u64 <= UINT32_MAX);
	uint32_t space_id = (uint32_t)u64;
	u64 = mp_decode_uint(&args);
	assert(u64 <= UINT32_MAX);
	uint32_t index_id = (uint32_t)u64;
	assert(mp_typeof(*args) == MP_ARRAY);
	const char *tuple_data = args;
	mp_next(&args);
	const char *tuple_data_end = args;
	assert(mp_typeof(*args) == MP_ARRAY);
	box_tuple_t *tuple = NULL;
	int index_base = 1;
	int rc = box_upsert(space_id, index_id, tuple_data, tuple_data_end,
			    args, args_end, index_base, &tuple);
	if (rc < 0)
		return -1;
	if (tuple == NULL)
		return 0;
	box_return_tuple(ctx, tuple);
	return 0;
}
