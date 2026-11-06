#include "box/xrow_update_field.h"
#include "fiber.h"
#include "memory.h"
#include "msgpuck.h"

#include <limits.h>
#include <stdlib.h>

__attribute__((constructor))
static void
setup(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
}

__attribute__((destructor))
static void
teardown(void)
{
	fiber_free();
	memory_free();
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	/*
	 * Input: [string or varbinary, [offset, cut_length, paste], ...].
	 * Each paste occurs only once, so the resulting string cannot be
	 * longer than the input and therefore its length also fits in an
	 * int32.
	 */
	if (size == 0 || size >= INT32_MAX)
		return -1;
	const char *pos = (const char *)data;
	const char *end = pos + size;
	if (mp_check(&pos, end) != 0 || pos != end)
		return -1;
	pos = (const char *)data;
	if (mp_typeof(*pos) != MP_ARRAY)
		return -1;
	uint32_t count = mp_decode_array(&pos);
	/* [string or varbinary, [offset, cut_length, paste], ...] */
	if (count < 2 || (mp_typeof(*pos) != MP_STR &&
			  mp_typeof(*pos) != MP_BIN))
		return -1;
	struct xrow_update_scalar scalar;
	xrow_update_mp_read_scalar(&pos, &scalar);
	struct region *region = &fiber()->gc;
	size_t region_svp = region_used(region);
	for (uint32_t i = 1; i < count; ++i) {
		/* [offset, cut_length, paste] */
		if (mp_typeof(*pos) != MP_ARRAY || mp_decode_array(&pos) != 3)
			break;
		struct xrow_update_op op = {.opcode = ':'};
		struct xrow_update_arg_splice *arg = &op.arg.splice;
		if (mp_read_int32(&pos, &arg->offset) != 0 ||
		    mp_read_int32(&pos, &arg->cut_length) != 0)
			break;
		if (mp_typeof(*pos) == MP_STR)
			arg->paste = mp_decode_str(&pos, &arg->paste_length);
		else if (mp_typeof(*pos) == MP_BIN)
			arg->paste = mp_decode_bin(&pos, &arg->paste_length);
		else
			break;
		if (xrow_update_op_do_splice(&op, &scalar) != 0)
			continue;
		/*
		 * Splice stores pointers to string chunks. Copy their data to
		 * detect out-of-bounds accesses caused by invalid chunk
		 * pointers or lengths.
		 */
		uint32_t out_size = xrow_update_scalar_sizeof(&scalar);
		char *out = xmalloc(out_size);
		uint32_t written = xrow_update_store_scalar(&scalar, NULL, out,
							    out + out_size);
		assert(written == out_size);
		const char *check = out;
		int rc = mp_check(&check, out + out_size);
		assert(rc == 0 && check == out + out_size);
		free(out);
	}
	diag_clear(diag_get());
	region_truncate(region, region_svp);
	return 0;
}
