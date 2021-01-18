#include "memory.h"
#include "fiber.h"
#include "tuple.h"

#include <benchmark/benchmark.h>

#include <cstdlib>
#include <vector>
#include <iostream>

enum {
	FIELD_COUNT_MAX = 1000,
	TUPLE_COUNT_MAX = 16384,
	TUPLE_MAX = 1048575,
};

static void
print_description_header(void)
{
	std::cout << std::endl << std::endl;
	std::cout << "******************************************************"
		"********************************" << std::endl;
	std::cout << "This benchmark consists of two parts: the first one "
		<< "checks the performance of access *" << std::endl
		<< "to indexed tuple fields, the second one checks "
		<< " the performance of memory allocation *" << std::endl
		<< "and deallocation operations for tuples for typical "
		<< "workload.                         *" << std::endl;
	std::cout << "To check access performance, test allocates 16384 objects "
		<< "with size near 255 (tiny   *" << std::endl << "tuples) or "
		<< "5000 (big tuples) bytes and push it in the vector. Then, in "
		<< "a loop, test  *" << std::endl << "checks the preformnance of "
		<< "access to indexed fields in random tuple.                 *"
		<< std::endl;
	std::cout << "To check allocation and deallocation performance, test "
		<< " allocates 1048575 objects    *" << std::endl << "with size "
		<< "near 255 (tiny tuples) or 5000 (big tuples) bytes and push "
		<< "it in the       *" << std::endl << "vector. Then in a loop "
		<< "test checks performance of one pair of memory allocation "
		<< "and  *" << std::endl << "deallocation operations.        "
		<< "                                                     *"
		<< std::endl;
	std::cout << "******************************************************"
		"********************************" << std::endl;
	std::cout << std::endl << std::endl;
}

static void
free_random_tuple(std::vector<struct tuple*>& v)
{
	unsigned i = rand() & (v.size() - 1);
	tuple_unref(v[i]);
	v[i] = v.back();
	v.pop_back();

}

static bool
tuple_alloc_default(std::vector<struct tuple*>& v, box_tuple_format_t *format,
		    char *tuple_buf, char *tuple_buf_end)
{
	struct tuple *tuple = tuple_new(format, tuple_buf, tuple_buf_end);
	if (tuple == NULL)
		return false;
	tuple_ref(tuple);
	try {
		v.push_back(tuple);
	} catch(std::bad_alloc& e) {
		tuple_unref(tuple);
		return false;
	}
	return true;
}

static bool
tuple_alloc(std::vector<struct tuple*>& v, box_tuple_format_t *format,
	    char *tuple_buf, unsigned tuple_buf_size, bool is_tiny)
{
	char *tuple_buf_end = tuple_buf;
	unsigned max_tuple_size = 0;
	unsigned field_size_max = mp_sizeof_uint(RAND_MAX);
	unsigned count = 0;

	if (is_tiny)
		max_tuple_size = UINT8_MAX;
	else
		max_tuple_size = tuple_buf_size;

	/*
	 * We don't know how many random items will fit in tuple with
	 * fixed size. That's why first we encode items, and then calculate
	 * array size for this items count. Then we move array items from the
	 * beginning of the buffer by the size of the array header.
	 */
	while (tuple_buf_end - tuple_buf <
	       max_tuple_size - field_size_max - mp_sizeof_array(count + 1)) {
		tuple_buf_end = mp_encode_uint(tuple_buf_end, rand());
		count++;
	}

	memmove(tuple_buf + mp_sizeof_array(count), tuple_buf, tuple_buf_end - tuple_buf);
	mp_encode_array(tuple_buf, count);

	return tuple_alloc_default(v, format, tuple_buf, tuple_buf_end + mp_sizeof_array(count));
}

static void
free_tuples(std::vector<struct tuple *> v)
{
	for (unsigned i = 0; i < v.size(); i++)
		tuple_unref(v[i]);
	v.clear();
}

static void
access_index_field(std::vector<struct tuple *> &v, unsigned field)
{
	uint32_t out;
	unsigned i = rand() & (v.size() - 1);
	::benchmark::DoNotOptimize(tuple_field_u32(v[i], field, &out));
}

static void
access_tuple_fields(benchmark::State& state)
{
	static const unsigned tuple_buf_size = 5 /*array header */ +
		FIELD_COUNT_MAX * mp_sizeof_uint(RAND_MAX);
	char tuple_buf[tuple_buf_size];
	std::vector<struct tuple *>tuples;
	unsigned count = state.range(0);
	bool is_tiny = state.range(1);
	unsigned field = state.range(2);
	uint32_t fieldno1[] = {1}, fieldno2[] = {field};
	uint32_t type1[] = {FIELD_TYPE_UNSIGNED};
	uint32_t type2[] = {FIELD_TYPE_UNSIGNED};
	box_key_def_t *key_defs[] = {
		box_key_def_new(fieldno1, type1, 1),
		box_key_def_new(fieldno2, type2, 1)
	};
	box_tuple_format_t *format = NULL;

	if (key_defs[0] == NULL || key_defs[1] == NULL) {
		state.SkipWithError("Failed to create key_defs");
		goto finish;
	}

	format = box_tuple_format_new(key_defs, 2);
	if (format == NULL) {
		state.SkipWithError("Failed to create tuple format");
		goto finish;
	}

	for (unsigned i = 0; i < count; i++) {
		if (!tuple_alloc(tuples, format, tuple_buf,
				 tuple_buf_size, is_tiny)) {
			state.SkipWithError("Failed to allocate tuple");
			goto finish;
		}
	}

	for (auto _ : state)
		access_index_field(tuples, field);

finish:
	free_tuples(tuples);
	if (format != NULL)
		tuple_format_unref(format);
	if (key_defs[0] != NULL)
		box_key_def_delete(key_defs[0]);
	if (key_defs[1] != NULL)
		box_key_def_delete(key_defs[1]);
}

static void
alloc_free_tuple(benchmark::State& state)
{
	static const unsigned tuple_buf_size = 5 /*array header */ +
		FIELD_COUNT_MAX * mp_sizeof_uint(RAND_MAX);
	char tuple_buf[tuple_buf_size];
	std::vector<struct tuple *>tuples;
	unsigned count = state.range(0);
	bool is_tiny = state.range(1);
	unsigned mask = (is_tiny ? 0x7f : 0xfff);
	unsigned size_min = (is_tiny ? 5 : UINT8_MAX + 1);
	unsigned size_max = (is_tiny ? UINT8_MAX : tuple_buf_size);

	/*
	 * Usually we need to create valid msgpuck array for tuple
	 * body, but here we check only allocation/deallocation
	 * preformance, so we don't need it.
	 */
	memset(tuple_buf, 0, sizeof(tuple_buf));
	for (unsigned i = 0; i < count; i++) {
		unsigned size = size_min + rand() & mask;
		if (size > size_max) {
			state.SkipWithError("Bad tuple size");
			goto finish;
		}
		if (! tuple_alloc_default(tuples, box_tuple_format_default(),
					  tuple_buf, tuple_buf + size)) {
			state.SkipWithError("Failed to allocate tuple");
			goto finish;
		}
	}

	for (auto _ : state) {
		unsigned size = size_min + rand() & mask;
		if (! tuple_alloc_default(tuples, box_tuple_format_default(),
					  tuple_buf, tuple_buf + size)) {
			state.SkipWithError("Failed to allocate tuple");
			goto finish;
		}
		free_random_tuple(tuples);
	}

finish:
	free_tuples(tuples);
}

BENCHMARK(access_tuple_fields)
	->ArgsProduct({{TUPLE_COUNT_MAX}, {0, 1}, {1, 2, 8, 32}})
	->ArgNames({"tuples count", "is_tiny", "access field"});

BENCHMARK(alloc_free_tuple)
	->ArgsProduct({{TUPLE_MAX}, {0, 1}})
	->ArgNames({"tuples count", "is_tiny"});

int main(int argc, char** argv)
{
	::benchmark::Initialize(&argc, argv);
	if (::benchmark::ReportUnrecognizedArguments(argc, argv))
		return 1;

	print_description_header();
	memory_init();
	fiber_init(fiber_c_invoke);
	tuple_init(NULL);

	::benchmark::RunSpecifiedBenchmarks();

	tuple_free();
	fiber_free();
	memory_free();
}
