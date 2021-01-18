#include "memory.h"
#include "fiber.h"
#include "tuple.h"

#include <benchmark/benchmark.h>

#include <vector>

const size_t amount = 1000, index_count = 4;
char **start;
char **end;
std::vector<struct tuple *> tuples;
std::vector<struct tuple *> big_tuples;
std::vector<struct tuple *> tiny_tuples;

static void
create_tuple(benchmark::State& state)
{
	size_t i = 0;
	for (auto _ : state) {
		struct tuple *tuple = tuple_new(box_tuple_format_default(),
						start[i % amount],
						end[i % amount]);
		tuple_ref(tuple);
		tuple_unref(tuple);
		i++;
	}
}
BENCHMARK(create_tuple);

static inline int
access_fields(struct tuple *tuple)
{
	int sum = 0;
	sum += tuple->refs;
	sum += tuple->format_id;
	sum += tuple_bsize(tuple);
	sum += tuple_data_offset(tuple);
	sum += tuple_is_dirty(tuple);
	return sum;
}

static void
access_tuple_fields(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = tuples[i++ % tuples.size()];
		sum += access_fields(tuple);
	}
	assert(sum > 0);
}
BENCHMARK(access_tuple_fields);

static void
access_tiny_tuple_fields(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = tiny_tuples[i++ % tiny_tuples.size()];
		sum += access_fields(tuple);
	}
	assert(sum > 0);
}
BENCHMARK(access_tiny_tuple_fields);

static void
access_big_tuple_fields(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = big_tuples[i++ % big_tuples.size()];
		sum += access_fields(tuple);
	}
	assert(sum > 0);
}
BENCHMARK(access_big_tuple_fields);

static inline int
access_data(struct tuple *tuple)
{
	uint32_t out;
	int sum = 0;
	for (size_t j = 0; j <= index_count; j++)
		sum += tuple_field_u32(tuple, j, &out);
	return sum;
}

static void
access_tuple_data(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = tuples[i++ % tuples.size()];
		access_data(tuple);
	}
	assert(sum == 0);
}
BENCHMARK(access_tuple_data);

static void
access_tiny_tuple_data(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = tiny_tuples[i++ % tiny_tuples.size()];
		access_data(tuple);
	}
	assert(sum == 0);
}
BENCHMARK(access_tiny_tuple_data);

static void
access_big_tuple_data(benchmark::State& state)
{
	size_t i = 0;
	int64_t sum = 0;
	for (auto _ : state) {
		struct tuple *tuple = big_tuples[i++ % big_tuples.size()];
		access_data(tuple);
	}
	assert(sum == 0);
}
BENCHMARK(access_big_tuple_data);

int main(int argc, char **argv)
{
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	char *alloc = (char *)malloc(amount * 5 + amount * (amount - 1) * 2);
	start = (char **)calloc(amount, sizeof(char *));
	end = (char **)calloc(amount, sizeof(char *));
	uint32_t data_size = index_count;
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

	uint32_t fieldno0[] = {0};
	uint32_t fieldno1[] = {1};
	uint32_t fieldno2[] = {2, 3};
	uint32_t type1[] = {FIELD_TYPE_UNSIGNED};
	uint32_t type2[] = {FIELD_TYPE_UNSIGNED, FIELD_TYPE_UNSIGNED};
	box_key_def_t *key_defs[] = {
		box_key_def_new(fieldno0, type1, 1),
		box_key_def_new(fieldno1, type1, 1),
		box_key_def_new(fieldno2, type2, 2)};
	box_tuple_format_t *format = box_tuple_format_new(key_defs, 3);
	for (size_t i = 0; i < amount; i++) {
		struct tuple *tuple = tuple_new(format, start[i], end[i]);
		tuple_ref(tuple);
		if (tuple_bsize(tuple) <= UINT8_MAX)
			tiny_tuples.push_back(tuple);
		else
			big_tuples.push_back(tuple);
		tuples.push_back(tuple);
	}

	::benchmark::Initialize(&argc, argv);
	if (::benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
	::benchmark::RunSpecifiedBenchmarks();

	free(alloc);
	free(start);
	free(end);
	for (auto tuple: tuples)
		tuple_unref(tuple);

	tuple_free();
	fiber_free();
	memory_free();
}