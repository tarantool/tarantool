#include "perf.h"

#include "memory.h"
#include "fiber.h"
#include "tuple.h"

#include <math.h>

/**
 * This function creates new tuple with refs == 1.
 */
static inline struct tuple *
create_tuple(const char *start, const char *end)
{
	struct tuple *ret = tuple_new(box_tuple_format_default(), start, end);
	tuple_ref(ret);
	return ret;
}

struct alloc_perf {
	struct perf_time time;
	uint64_t overall_size;
};

static inline void
test_unref(struct tuple **array, int size, struct alloc_perf *perf)
{
	struct cur_time start = perf_get_time(NULL);
	for (int i = 0; i < size; i++) {
		perf->overall_size += tuple_bsize(array[i]);
		tuple_unref(array[i]);
	}
	perf_add_time(&perf->time, perf_count(start));
}

static inline void
test_tuple_access(struct tuple **array, int size)
{
	uint64_t sum = 0,
		 counter = 0;
	uint32_t out;
	struct cur_time start = perf_get_time(NULL);
	for (int i = 0; i < 100; i++) {
		for (int j = 0; j < size; j++) {
			struct tuple *tuple = array[j];
			sum += (tuple_bsize(tuple) <= UINT8_MAX) != tuple->is_tiny;
			sum += tuple_is_dirty(tuple);
			sum += tuple_data_or_null(tuple) == NULL;
			for (uint32_t k = 0; k < tuple_field_count(tuple); k++) {
				counter++;
				sum += tuple_field_u32(tuple, k, &out);
			}
		}
	}
	struct perf_time time = perf_count(start);
	assert(sum == 0);
	printf("%s\n", perf_json_result("Tuple access in different ways",
					"access / s",
					counter / time.wall_time));
	printf("%s\n", perf_json_result("Tuple access in different ways",
					"access / s",
					counter / time.cpu_time));
}

static inline void
test_alloc(struct tuple **array, char *alloc, size_t data_size,
	   float factor, int amount, struct alloc_perf *perf)
{
	for (int i = 0; i < amount; i++) {
		char *end = alloc;
		end = mp_encode_array(end, data_size);
		for (size_t j = 0; j < data_size; j++)
			end = mp_encode_uint(end, j);
		struct cur_time start = perf_get_time(NULL);
		array[i] = create_tuple(alloc, end);
		perf_add_time(&perf->time, perf_count(start));
		perf->overall_size += tuple_bsize(array[i]);
		data_size = ceil(factor * data_size);
	}
}

int
main(void)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	struct alloc_perf alloc_perf, delete_perf;
	delete_perf.time = alloc_perf.time = perf_init();
	delete_perf.overall_size = alloc_perf.overall_size = 0;
	int repeat_alloc = 10000;
	int amount = 500;
	float factor = 1.001;
	char *alloc = malloc(amount * sizeof(uint));
	struct tuple **array = calloc(amount, sizeof(struct tuple *));
	for (int i = 0; i < repeat_alloc; i++) {
		test_alloc(array, alloc, 1, factor, amount, &alloc_perf);
		if (i == repeat_alloc - 1) break;
		test_unref(array, amount, &delete_perf);
	}
	printf("%s\n%s\n",
	       perf_json_result(tt_sprintf("Tuple allocation, "
					   "bsize from %u to %u with factor %f",
					   tuple_bsize(array[0]),
					   tuple_bsize(array[amount - 1]),
					   factor),
				"bytes / s", alloc_perf.overall_size /
					     alloc_perf.time.wall_time),
	       perf_json_result(tt_sprintf("Tuple allocation, "
					   "bsize from %u to %u with factor %f",
					   tuple_bsize(array[0]),
					   tuple_bsize(array[amount - 1]),
					   factor),
				"bytes / s", alloc_perf.overall_size /
					     alloc_perf.time.cpu_time));
	printf("%s\n%s\n",
	       perf_json_result(tt_sprintf("Tuple deletion, "
					   "bsize from %u to %u with factor %f",
					   tuple_bsize(array[0]),
					   tuple_bsize(array[amount - 1]),
					   factor),
				"bytes / s", delete_perf.overall_size /
					     delete_perf.time.wall_time),
	       perf_json_result(tt_sprintf("Tuple deletion, "
					   "bsize from %u to %u with factor %f",
					   tuple_bsize(array[0]),
					   tuple_bsize(array[amount - 1]),
					   factor),
				"bytes / s", delete_perf.overall_size /
					     delete_perf.time.cpu_time));

	test_tuple_access(array, amount);

	test_unref(array, amount, &delete_perf);

	free(array);
	free(alloc);

	tuple_free();
	fiber_free();
	memory_free();
}
