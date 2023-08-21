#include "memory.h"
#include "fiber.h"
#include "tuple.h"
#include "memtx_engine.h"
#include <allocator.h>

#include <benchmark/benchmark.h>

const size_t NUM_TEST_TUPLES = 4096;
const size_t MAX_TUPLE_DATA_SIZE = 512;

// Class that creates and destroys tuple format for private memtx engine.
class MemtxEngine {
public:
	static MemtxEngine &instance()
	{
		static MemtxEngine instance;
		return instance;
	}
	struct tuple_format *format() { return fmt; }
	struct key_def *key_def() { return kd; }
private:
	MemtxEngine()
	{
		memory_init();
		fiber_init(fiber_c_invoke);
		region_alloc(&fiber()->gc, 4);
		tuple_init(NULL);

		memset(&memtx, 0, sizeof(memtx));

		quota_init(&memtx.quota, QUOTA_MAX);

		int rc;
		rc = slab_arena_create(&memtx.arena, &memtx.quota,
				       16 * 1024 * 1024, 16 * 1024 * 1024,
				       SLAB_ARENA_PRIVATE);
		if (rc != 0)
			abort();

		slab_cache_create(&memtx.slab_cache, &memtx.arena);

		float actual_alloc_factor;
		allocator_settings alloc_settings;
		allocator_settings_init(&alloc_settings, &memtx.slab_cache,
					16, 8, 1.1, &actual_alloc_factor,
					&memtx.quota);
		SmallAlloc::create(&alloc_settings);
		memtx_set_tuple_format_vtab("small");

		memtx.max_tuple_size = 1024 * 1024;

		struct key_part_def kdp{0};
		kdp.fieldno = 4;
		kdp.type = FIELD_TYPE_UNSIGNED;
		kd = key_def_new(&kdp, 1, 0);
		fmt = simple_tuple_format_new(&memtx_tuple_format_vtab,
					      &memtx, &kd, 1);
		tuple_format_ref(fmt);
	}
	~MemtxEngine()
	{
		key_def_delete(kd);
		tuple_format_unref(fmt);
		tuple_free();
		SmallAlloc::destroy();
		slab_cache_destroy(&memtx.slab_cache);
		tuple_arena_destroy(&memtx.arena);
		fiber_free();
		memory_free();
	}

	struct memtx_engine memtx;
	struct key_def *kd;
	struct tuple_format *fmt;
};

// Generator of random msgpack array.
class MpData {
public:
	const char *begin() const { return data; }
	const char *end() const { return data_end; }
	MpData()
	{
		uint64_t r1 = (uint64_t)rand() * 1024 + rand();
		uint64_t r2 = (uint64_t)rand() * 1024 + rand();
		uint64_t r3 = (uint64_t)rand() * 1024 + rand();
		const size_t common_size = 12 + mp_sizeof_uint(r1) +
			mp_sizeof_uint(r2) + mp_sizeof_uint(r3);
		size_t add_bytes = rand() % (MAX_TUPLE_DATA_SIZE - common_size);
		size_t add_nums = add_bytes / 5;

		data_end = data;
		data_end = mp_encode_array(data_end, 5 + add_nums);
		data_end = mp_encode_uint(data_end, r1);
		data_end = mp_encode_str(data_end, "hello", 5);
		data_end = mp_encode_nil(data_end);
		data_end = mp_encode_uint(data_end, r2);
		data_end = mp_encode_uint(data_end, r3);
		if (data_end - data > common_size)
			abort();
		for (size_t i = 0; i < add_nums; i++)
			data_end = mp_encode_uint(data_end, 0xFFFFFF);
		if (data_end - data > MAX_TUPLE_DATA_SIZE)
			abort();
	}
private:
	char data[MAX_TUPLE_DATA_SIZE];
	char *data_end;
};

// Generator of set of random msgpack arrays.
class MpDataSet {
public:
	static MpDataSet &instance()
	{
		static MpDataSet instance;
		return instance;
	}
	const MpData &operator[](size_t i) const { return data[i]; }
private:
	MpData data[NUM_TEST_TUPLES];
};

// Generator of a set of random tuples.
class TestTuples {
public:
	TestTuples()
	{
		format = MemtxEngine::instance().format();
		tuple_format_ref(format);
		MpDataSet &dataset = MpDataSet::instance();

		for (size_t i = 0; i < NUM_TEST_TUPLES; i++) {
			data[i] = box_tuple_new(format,
						dataset[i].begin(),
						dataset[i].end());
			tuple_ref(data[i]);
		}
	}
	~TestTuples()
	{
		for (size_t i = 0; i < NUM_TEST_TUPLES; i++)
			tuple_unref(data[i]);

		tuple_format_unref(format);
	}
	struct tuple *operator[](size_t i) { return data[i]; }

private:
	struct tuple_format *format;
	struct tuple *data[NUM_TEST_TUPLES];
};

// box_tuple_new benchmark.
static void
bench_tuple_new(benchmark::State& state)
{
	size_t total_count = 0;

	struct tuple_format *format = MemtxEngine::instance().format();
	MpDataSet &dataset = MpDataSet::instance();
	struct tuple *tuples[NUM_TEST_TUPLES];
	size_t i = 0;

	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			state.PauseTiming();
			for (size_t k = 0; k < NUM_TEST_TUPLES; k++)
				tuple_unref(tuples[k]);
			i = 0;
			state.ResumeTiming();
		}
		tuples[i] = box_tuple_new(format,
					  dataset[i].begin(),
					  dataset[i].end());
		tuple_ref(tuples[i]);
		++i;
	}
	total_count += i;
	state.SetItemsProcessed(total_count);

	for (size_t k = 0; k < i; k++)
		tuple_unref(tuples[k]);
}

BENCHMARK(bench_tuple_new);

// memtx_tuple_delete benchmark.
static void
bench_tuple_delete(benchmark::State& state)
{
	size_t total_count = 0;

	struct tuple_format *format = MemtxEngine::instance().format();
	MpDataSet &dataset = MpDataSet::instance();
	struct tuple *tuples[NUM_TEST_TUPLES];

	size_t i = NUM_TEST_TUPLES;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			state.PauseTiming();
			for (size_t k = 0; k < NUM_TEST_TUPLES; k++) {
				tuples[k] = box_tuple_new(format,
							  dataset[k].begin(),
							  dataset[k].end());
				tuple_ref(tuples[k]);
			}
			i = 0;
			state.ResumeTiming();
		}
		tuple_unref(tuples[i++]);
	}
	total_count += i;
	state.SetItemsProcessed(total_count);

	for (size_t k = i; k < NUM_TEST_TUPLES; k++)
		tuple_unref(tuples[k]);
}

BENCHMARK(bench_tuple_delete);

static void
bench_tuple_ref_unref_low(benchmark::State& state)
{
	TestTuples tuples;
	size_t total_count = 0;
	const size_t NUM_REFS = 32;
	for (auto _ : state) {
		for (size_t k = 0; k < NUM_REFS; k++)
			for (size_t i = 0; i < NUM_TEST_TUPLES; i++)
				tuple_ref(tuples[i]);
		for (size_t k = 0; k < NUM_REFS; k++)
			for (size_t i = 0; i < NUM_TEST_TUPLES; i++)
				tuple_unref(tuples[i]);
		total_count += NUM_REFS * NUM_TEST_TUPLES;
	}
	state.SetItemsProcessed(total_count);
}

BENCHMARK(bench_tuple_ref_unref_low);

static void
bench_tuple_ref_unref_high(benchmark::State& state)
{
	TestTuples tuples;
	size_t total_count = 0;
	const size_t NUM_REFS = 1024;
	for (auto _ : state) {
		for (size_t k = 0; k < NUM_REFS; k++)
			for (size_t i = 0; i < NUM_TEST_TUPLES; i++)
				tuple_ref(tuples[i]);
		for (size_t k = 0; k < NUM_REFS; k++)
			for (size_t i = 0; i < NUM_TEST_TUPLES; i++)
				tuple_unref(tuples[i]);
		total_count += NUM_REFS * NUM_TEST_TUPLES;
	}
	state.SetItemsProcessed(total_count);
}

BENCHMARK(bench_tuple_ref_unref_high);

// struct tuple member access benchmark.
static void
tuple_access_members(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		struct tuple *t = tuples[i++];
		// Previously tuple had is_dirty bit field, which was later
		// replaced with tuple flags. So to avoid changing test
		// semantics we check now if tuple has corresponding flag.
		benchmark::DoNotOptimize(tuple_has_flag(t, TUPLE_IS_DIRTY));
		benchmark::DoNotOptimize(uint16_t(t->format_id));
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_access_members);

// tuple_data benchmark.
static void
tuple_access_data(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		struct tuple *t = tuples[i++];
		benchmark::DoNotOptimize(*tuple_data(t));
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_access_data);

// tuple_data_range benchmark.
static void
tuple_access_data_range(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		struct tuple *t = tuples[i++];
		uint32_t size;
		benchmark::DoNotOptimize(*tuple_data_range(t, &size));
		benchmark::DoNotOptimize(size);
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_access_data_range);

// benchmark of access of non-indexed field.
static void
tuple_access_unindexed_field(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		struct tuple *t = tuples[i++];
		benchmark::DoNotOptimize(*tuple_field(t, 3));
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_access_unindexed_field);

// benchmark of access of indexed field.
static void
tuple_access_indexed_field(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		struct tuple *t = tuples[i];
		benchmark::DoNotOptimize(*tuple_field(t, 4));
		++i;
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_access_indexed_field);

// benchmark of tuple compare.
static void
tuple_tuple_compare(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t j = 0;
	struct key_def *kd = MemtxEngine::instance().key_def();
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		if (j >= NUM_TEST_TUPLES)
			j -= NUM_TEST_TUPLES;
		struct tuple *t1 = tuples[i];
		struct tuple *t2 = tuples[j];
		benchmark::DoNotOptimize(tuple_compare(t1, 0, t2, 0, kd));
		++i;
		j += 3;
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_tuple_compare);

// benchmark of tuple hints compare.
static void
tuple_tuple_compare_hint(benchmark::State& state)
{
	TestTuples tuples;
	size_t i = 0;
	size_t j = 0;
	struct key_def *kd = MemtxEngine::instance().key_def();
	size_t total_count = 0;
	for (auto _ : state) {
		if (i == NUM_TEST_TUPLES) {
			total_count += i;
			i = 0;
		}
		if (j >= NUM_TEST_TUPLES)
			j -= NUM_TEST_TUPLES;
		struct tuple *t1 = tuples[i];
		struct tuple *t2 = tuples[j];
		hint_t h1 = tuple_hint(t1, kd);
		hint_t h2 = tuple_hint(t2, kd);
		benchmark::DoNotOptimize(tuple_compare(t1, h1, t2, h2, kd));
		++i;
		j += 3;
	}
	total_count += i;
	state.SetItemsProcessed(total_count);
}

BENCHMARK(tuple_tuple_compare_hint);

BENCHMARK_MAIN();

#include "debug_warning.h"
