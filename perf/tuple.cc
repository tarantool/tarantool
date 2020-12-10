#include "memory.h"
#include "fiber.h"
#include "tuple.h"

#include <math.h>

#include <benchmark/benchmark.h>

size_t amount;
char **start;
char **end;
struct tuple **tuples;

static void
create_tuple(benchmark::State& state)
{
	size_t i = 0, it = 0, ib = 0;
	for (auto _ : state) {
		struct tuple *tuple = tuple_new(box_tuple_format_default(),
						start[i % amount],
						end[i % amount]);
		tuples[i++ % amount] = tuple;
	}
}
BENCHMARK(create_tuple);

static inline int
access_fields(struct tuple *tuple)
{
	int sum = 0;
	sum += tuple->refs;
	sum += tuple->format_id;
	sum += tuple->bsize;
	sum += tuple->data_offset;
	sum += tuple->is_dirty;
	return sum;
}

static void
access_tuple_fields(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = tuples[i++ % amount];
		sum += access_fields(tuple);
	}
	assert(sum > 0);
}
BENCHMARK(access_tuple_fields);

static inline int
access_data(struct tuple *tuple)
{
	uint32_t out;
	int sum = 0;
	for (size_t j = 0; j < tuple_field_count(tuple); j++)
		sum += tuple_field_u32(tuple, j, &out);
	return sum;
}

static void
access_tuple_data(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = tuples[i++ % amount];
		access_data(tuple);
	}
	assert(sum == 0);
}
BENCHMARK(access_tuple_data);

int main(int argc, char **argv)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	amount = 1000;
	char *alloc = (char *)malloc(amount * 5 + amount * (amount - 1) * 2);
	start = (char **)calloc(amount, sizeof(char *));
	end = (char **)calloc(amount, sizeof(char *));
	uint32_t data_size = 0;
	start[0] = alloc;
	for (size_t i = 0; i < amount; i++) {
		char *cur = start[i];
		cur = mp_encode_array(cur, ++data_size);
		for (size_t j = 0; j < data_size; j++)
			cur = mp_encode_uint(cur, j);
		end[i] = cur;
		if (i + 1 < amount)
			start[i + 1] = cur;
	}

	tuples = (struct tuple **)calloc(amount, sizeof(struct tuple *));

	::benchmark::Initialize(&argc, argv);
	if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
	::benchmark::RunSpecifiedBenchmarks();

	free(alloc);
	free(start);
	free(end);
	for (size_t i = 0; i < amount; i++) {
		struct tuple *tuple = tuples[i];
		tuple_ref(tuple);
		tuple_unref(tuple);
	}
	free(tuples);

	tuple_free();
	fiber_free();
	memory_free();
}
