#include "box/tuple.h"
#include "box/tuple_format.h"
#include "box/xrow_update.h"
#include "fiber.h"
#include "memory.h"
#include "mp_decimal.h"
#include "mp_extension_types.h"
#include "msgpuck.h"

#include <limits.h>

static int
check_ext(int8_t type, const char *data, uint32_t len)
{
	/* Update assumes decimal payloads have already been validated. */
	return type == MP_DECIMAL ? mp_validate_decimal(data, len) : 0;
}

__attribute__((constructor))
static void
setup(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init();
	mp_check_ext_data = check_ext;
}

__attribute__((destructor))
static void
teardown(void)
{
	tuple_free();
	fiber_free();
	memory_free();
}

static void
fuzz_update(const char *tuple, const char *ops, const char *end, int index_base)
{
	struct xrow_update update;
	xrow_update_init(&update, index_base);
	const char *pos = tuple;
	uint32_t field_count = mp_decode_array(&pos);
	if (xrow_update_read_ops(&update, ops, end, tuple_format_runtime->dict,
				 field_count) != 0)
		return;
	/* Bound the nil-filled gap to keep generated tuples small. */
	const int64_t max_nil_gap = 64;
	for (uint32_t i = 0; i < update.op_count; ++i) {
		struct xrow_update_op *op = &update.ops[i];
		if ((op->opcode == '=' || op->opcode == '!') &&
		    (int64_t)op->field_no > (int64_t)field_count + max_nil_gap)
			return;
	}
	uint32_t out_size;
	const char *out = xrow_update_execute(ops, end, tuple, ops,
					      tuple_format_runtime, &out_size,
					      index_base, NULL);
	if (out != NULL) {
		assert(out_size > 0 && mp_typeof(*out) == MP_ARRAY);
		const char *check = out;
		int rc = mp_check(&check, out + out_size);
		assert(rc == 0 && check == out + out_size);
	}
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/*
	 * Input: [tuple, [[opcode, field_no or JSON path, args...], ...]].
	 * Operations may change field types and positions, or update the
	 * same nested array or map multiple times.
	 */
	if (size == 0 || size >= INT32_MAX)
		return -1;
	const char *pos = (const char *)data;
	const char *end = pos + size;
	if (mp_check(&pos, end) != 0 || pos != end)
		return -1;
	pos = (const char *)data;
	if (mp_typeof(*pos) != MP_ARRAY || mp_decode_array(&pos) != 2)
		return -1;
	const char *tuple = pos;
	if (mp_typeof(*tuple) != MP_ARRAY)
		return -1;
	mp_next(&pos);
	const char *ops = pos;
	if (mp_typeof(*ops) != MP_ARRAY)
		return -1;
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	for (int index_base = 0; index_base <= 1; ++index_base) {
		fuzz_update(tuple, ops, end, index_base);
		diag_clear(diag_get());
		region_truncate(region, region_svp);
	}
	return 0;
}
