#include <algorithm>
#include <climits>
#include <cstring>
#include <random>
#include <vector>
#include <unordered_set>

#include <benchmark/benchmark.h>

// This test contains benchmarks for Light - data structure implementing
// Tarantool HASH index and being hash table under the hood.
//
// Let's use raw payload (similar to native Tarantool tuples) as values.
// Test scenarios:
//  - Inserts only;
//  - Search only (by value), no misses;
//  - Search only (by value) with misses;
//  - Search by key;
//  - Sequence iteration;
//  - Inserts after erase;
//  - Inserts alongside with lookups;
//  - Search after erase;
//  - Deletes.
//
// To compare numbers given by this benchmark we also run a few tests with
// std::unordered_map at the end.


// Tuple size in fact does not really matter since in Tarantool we store
// pointers to them. So let's use just random one :)
constexpr static std::size_t TUPLE_SIZE = 1 << 5;

// We measure perf on large datasets: min number of elements is
// considered to be 10k; max - 1M (which I guess covers the most
// popular amount of data stored in Tarantool indexes).
constexpr static std::size_t TUPLE_COUNT_MIN = 10000;
constexpr static std::size_t TUPLE_COUNT_MAX = 100 * TUPLE_COUNT_MIN;
constexpr static std::size_t TUPLE_COUNT_MULTIPLIER = 10;

////////////////////////////// Data Definitions ////////////////////////////////////////////////////////////////////////

using Key_t   = uint32_t;
using Hash_t  = uint32_t;

// 'size' can be skipped in fact (since it is the same for all tuples).
// Let's keep it just in case (and to make benchmark look a bit closer
// to real world). In order to compare two TupleRaw we use memcmp().
struct TupleRaw {
	std::size_t size;
	char data[TUPLE_SIZE];

	bool operator==(const TupleRaw &a) const
	{
		return std::memcmp(data, a.data, size) == 0;
	}
	// Key is considered to be first 4 bytes of data.
	Key_t key() const
	{
		Key_t me;
		memcpy(&me, data, sizeof(me));
		return me;
	}
	bool operator==(Key_t a) const
	{
		return key() == a;
	}
};

// Light hash map implementation does not include any hashing function;
// it's up to user to choose the proper one. Let's use quite primitive
// yet good enough: Fowler–Noll–Vo (-1a).
namespace Hash {
	static constexpr std::uint32_t offset_basis = 0x811c9dc5;
	static constexpr std::uint32_t prime = 0x01000193;

	Hash_t
	FNV_hash(const char *ptr, std::size_t len)
	{
		std::uint32_t hash = offset_basis;
		for (std::size_t i = 0; i < len; ++i) {
			hash ^= ptr[i];
			hash *= prime;
		}
		return hash;
	}
};

// Wrapper to move hash calculation to setup stage (in order to make
// performance benchmark results cleaner).
// Also, for data simplicity of data generation extracted key is stored
// here. Note that the hash table must not use this key, it is only
// for benchmarks.
struct TupleRef {
	TupleRef(const TupleRaw *t) : tuple(t), hash(Hash::FNV_hash(t->data, t->size)), key(t->key()) { }
	const TupleRaw *tuple;
	Key_t key;
	Hash_t hash;
};

namespace Hash {
	// Since std::unordered_set does not provide an ability to specify
	// hash value beforehand, let's use struct Tuple as values with
	// pre-calculated hashes (yes, unordered_set stores hash values on its
	// own but we have nothing left to do here to make it closer to
	// Light bench).
	struct TupleHash {
		std::size_t operator()(const TupleRef &t) const
		{
			return t.hash;
		}
	};
}

struct TupleEqual
{
	bool operator()(const TupleRef &lhs, const TupleRef &rhs) const noexcept
	{
		return *lhs.tuple == *rhs.tuple;
	}
};

// Simple generator of uniformly distributed random bytes.
struct RandomBytesGenerator {
	RandomBytesGenerator() : gen(std::random_device()()) { }
	void prebuf()
	{
		using rand_t = std::mt19937_64::result_type;
		static_assert(sizeof(buf) % sizeof(rand_t) == 0, "wtf");
		for (pos = 0; pos < sizeof(buf); pos += sizeof(rand_t)) {
			rand_t r = gen();
			memcpy(&buf[pos], &r, sizeof(r));
		}
	}
	unsigned char get()
	{
		if (pos == 0)
			prebuf();
		return buf[--pos];
	}
	unsigned char buf[1024];
	size_t pos = 0;
	std::mt19937_64 gen;
};

// Large chunk of continuous memory initialized with random bytes.
struct TupleHolder {
	TupleHolder(std::size_t tuple_count)
	{
		storage.resize(tuple_count);
		for (auto& tuple : storage) {
			tuple.size = TUPLE_SIZE;
			for (char &c : tuple.data)
				c = random_generator.get();
		}
		tuples.reserve(tuple_count);
		for (std::size_t i = 0; i < tuple_count; ++i)
			tuples.emplace_back(&storage[i]);
	}

	void shuffle()
	{
		// Instead of generating new values in most cases it's enough
		// to shuffle old ones.
		std::random_shuffle(tuples.begin(), tuples.end());
	}

	TupleHolder(const TupleHolder &) = delete;
	TupleHolder(TupleHolder &&) = delete;

	std::vector<TupleRaw> storage;
	std::vector<TupleRef> tuples;
	static RandomBytesGenerator random_generator;
};

RandomBytesGenerator TupleHolder::random_generator{};

////////////////////////////// Light Definitions ///////////////////////////////////////////////////////////////////////

namespace {
	bool
	tuple_equals(const TupleRaw *t1, const TupleRaw *t2)
	{
		assert(t2->size == t1->size);
		return *t1 == *t2;
	}

	bool
	key_equals(const TupleRaw *t1, Key_t k2)
	{
		return t1->key() == k2;
	}

	enum {
		KB = 1 << 10,
	};

	static constexpr std::size_t light_extent_size = 16 * KB;
	static std::size_t extents_count = 0;

	inline void *
	light_malloc_extend(void *ctx)
	{
		std::size_t *p_extents_count = (size_t *) ctx;
		assert(p_extents_count == &extents_count);
		++*p_extents_count;
		return malloc(light_extent_size);
	}

	inline void
	light_free_extend(void *ctx, void *p)
	{
		size_t *p_extents_count = (size_t *) ctx;
		assert(p_extents_count == &extents_count);
		--*p_extents_count;
		free(p);
	}
}; // namespace

#define LIGHT_NAME
#define LIGHT_DATA_TYPE const TupleRaw *
#define LIGHT_KEY_TYPE Key_t
#define LIGHT_CMP_ARG_TYPE int
#define LIGHT_EQUAL(a, b, arg) tuple_equals(a, b)
#define LIGHT_EQUAL_KEY(a, b, arg) key_equals(a, b)

#include "salad/light.h"

////////////////////////////// Fixture /////////////////////////////////////////////////////////////////////////////////

template<typename T>
class HTBench : public ::benchmark::Fixture {
protected:
	void
	Fill(std::vector<TupleRef>::iterator &&begin, std::vector<TupleRef>::iterator &&end) noexcept
	{
		std::for_each(begin, end,
			[this](const TupleRef &t){ hash_table.insert(t); });
	}

	void
	Erase(std::vector<TupleRef>::iterator &&begin, std::vector<TupleRef>::iterator &&end) noexcept
	{
		std::for_each(begin, end,
			[this](const TupleRef &t){ hash_table.erase(t); });
	}

	// It is required to cleanup the whole table between state iterations
	// to keep fixed entries count.
	void
	Reset()
	{
		hash_table.clear();
	}

	//////////////////////////////// BENCHMARKS ////////////////////////////////////////////////////////////////////

	// Insert random values into hash table; no warm up - hash table is empty at the benchmark start.
	void
	InsertRandValue(benchmark::State& state)
	{
		std::size_t insertion_count = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			// state.range(0) stores current range argument.
			TupleHolder data(state.range(0));
			state.ResumeTiming();

			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.insert(v));
				insertion_count++;
			}
		}
		state.SetItemsProcessed(insertion_count);
	}

	void
	InsertRandValueReserve(benchmark::State& state)
	{
		std::size_t insertion_count = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			state.ResumeTiming();

			hash_table.reserve(state.range(0));
			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.insert(v));
				insertion_count++;
			}
		}
		state.SetItemsProcessed(insertion_count);
	}

	// Lookup random values; value is supposed to be presented in hashtable (no misses).
	void
	FindRandValue(benchmark::State& state)
	{
		std::size_t lookup_count = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			Fill(data.tuples.begin(), data.tuples.end());
			data.shuffle();
			state.ResumeTiming();

			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.find(v));
				lookup_count++;
			}
		}
		state.SetItemsProcessed(lookup_count);
	}

	// Lookup random values (most of them should not be presented in the table).
	void
	FindRandValueWithMisses(benchmark::State& state)
	{
		std::size_t lookup_count = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			Fill(data.tuples.begin(), data.tuples.end());
			// Re-generate dataset so it contains random values.
			TupleHolder missing_values(state.range(0));
			state.ResumeTiming();

			for (const auto &v : missing_values.tuples) {
				benchmark::DoNotOptimize(hash_table.find(v));
				lookup_count++;
			}
		}
		state.SetItemsProcessed(lookup_count);
	}

	// Lookup random values by key.
	void
	FindRandByKey(benchmark::State& state)
	{
		std::size_t lookup_count = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			Fill(data.tuples.begin(), data.tuples.end());
			data.shuffle();
			state.ResumeTiming();

			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.find_key(v));
				lookup_count++;
			}
		}
		state.SetItemsProcessed(lookup_count);
	}

	// Sequence iteration over the hash table - starting from the first value.
	// Measurements include iterator dereference.
	void
	SequenceIteration(benchmark::State& state)
	{
		std::size_t processed = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			Fill(data.tuples.begin(), data.tuples.end());
			data.shuffle();
			state.ResumeTiming();

			processed += hash_table.iter_all();
		}
		state.SetItemsProcessed(processed);
	}

	// Fill in hash table, then erase all elements and re-fill it once again
	// (only insertion time is measured).
	void
	InsertAfterErase(benchmark::State& state)
	{
		std::size_t processed = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			Fill(data.tuples.begin(), data.tuples.end());
			Erase(data.tuples.begin(), data.tuples.end());
			data.shuffle();
			state.ResumeTiming();

			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.insert(v));
				processed++;
			}
		}
		state.SetItemsProcessed(processed);
	}

	// Fill in hash table, then erase half elements and process lookups
	// (only lookup time is measured).
	void
	FindAfterErase(benchmark::State& state)
	{
		std::size_t processed = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			// Double the data size so that after shrink it'll be of the
			// same size as in other benches.
			std::size_t data_size = state.range(0) << 1;
			TupleHolder data(data_size);
			Fill(data.tuples.begin(), data.tuples.end());
			Erase(data.tuples.begin(), data.tuples.begin() + (data_size >> 1));
			data.shuffle();
			state.ResumeTiming();

			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.find(v));
				processed++;
			}
		}
		state.SetItemsProcessed(processed);
	}

	// Random insert and random lookup. On each iteration it processes insert and
	// after that - lookup (half of lookups - misses).
	void
	InsertOrFind(benchmark::State& state)
	{
		std::size_t processed = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			state.ResumeTiming();

			auto lookup_iter = data.tuples.rbegin();
			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.insert(v));
				benchmark::DoNotOptimize(hash_table.find(*lookup_iter));
				lookup_iter++;
				processed += 2;
			}
		}
		state.SetItemsProcessed(processed);
	}

	// Delete random values till the hashtable is empty.
	void
	DeleteRandValue(benchmark::State& state)
	{
		std::size_t processed = 0;
		for (auto s : state) {
			state.PauseTiming();
			Reset();
			TupleHolder data(state.range(0));
			Fill(data.tuples.begin(), data.tuples.end());
			data.shuffle();
			state.ResumeTiming();

			for (const auto &v : data.tuples) {
				benchmark::DoNotOptimize(hash_table.erase(v));
				processed++;
			}
		}
		state.SetItemsProcessed(processed);
	}

	T hash_table;
};

class Light {
public:
	Light()
	{
		light_create(&ht, 0, light_extent_size, light_malloc_extend,
			     light_free_extend, &extents_count, nullptr);
	}
	~Light()
	{
		light_destroy(&ht);
	}

	// Functions return smth in order to suppress "error: invalid use of void expression".
	int
	erase(const TupleRef &tuple)
	{
		light_delete_value(&ht, tuple.hash, tuple.tuple);
		return 0;
	}

	int
	insert(const TupleRef &tuple)
	{
		light_insert(&ht, tuple.hash, tuple.tuple);
		return 0;
	}

	const TupleRaw *
	find(const TupleRef &tuple)
	{
		static TupleRaw DUMMY{0};
		uint32_t slot = light_find(&ht, tuple.hash, tuple.tuple);
		if (slot != light_end)
			return light_get(&ht, slot);
		return &DUMMY;
	}

	uint32_t
	find_key(const TupleRef &tuple)
	{
		return light_find_key(&ht, tuple.hash, tuple.key);
	}

	void
	clear()
	{
		light_destroy(&ht);
		light_create(&ht, 0, light_extent_size, light_malloc_extend,
			     light_free_extend, &extents_count, nullptr);
	}

	void
	reserve(std::size_t) { /* no-op*/ }

	std::size_t
	iter_all()
	{
		std::size_t processed = 0;
		struct light_iterator iter;
		light_iterator_begin(&ht, &iter);
		const TupleRaw **p = nullptr;
		while ((p = light_iterator_get_and_next(&ht, &iter)) != nullptr) {
			benchmark::DoNotOptimize((*p)->data);
			processed++;
		}
		return processed;
	}
private:
	struct light_core ht;
};

using USet = std::unordered_set<TupleRef, Hash::TupleHash, TupleEqual>;

class STL {
public:
	auto erase(const TupleRef &tuple) { return ht.erase(tuple); }
	auto insert(const TupleRef &tuple) { return ht.insert(tuple); }
	auto find(const TupleRef &tuple) { return ht.find(tuple); }
	auto find_key(const TupleRef &tuple) { return ht.find(tuple); }
	void clear() { ht.clear(); }
	void reserve(std::size_t n) { ht.reserve(n); }

	std::size_t
	iter_all()
	{
		std::size_t processed = 0;
		for (const auto itr : ht) {
			benchmark::DoNotOptimize(itr.tuple);
			processed++;
		}
		return processed;
	}

private:
	USet ht;
};

#define BENCHMARK_TEMPLATE_REGISTER(METHOD_NAME, HASH_TABLE_NAME) \
	BENCHMARK_TEMPLATE_DEFINE_F(HTBench, HASH_TABLE_NAME##METHOD_NAME, HASH_TABLE_NAME)(benchmark::State& state) \
	{ HTBench::METHOD_NAME(state); } \
	BENCHMARK_REGISTER_F(HTBench, HASH_TABLE_NAME##METHOD_NAME)-> \
		RangeMultiplier(TUPLE_COUNT_MULTIPLIER)-> \
			Range(TUPLE_COUNT_MIN, TUPLE_COUNT_MAX)

#define BENCHMARK_TEMPLATE_REGISTER_LIGHT(METHOD_NAME) \
	BENCHMARK_TEMPLATE_REGISTER(METHOD_NAME, Light)

#define BENCHMARK_TEMPLATE_REGISTER_STL(METHOD_NAME) \
	BENCHMARK_TEMPLATE_REGISTER(METHOD_NAME, STL)

#define BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(METHOD_NAME) \
	BENCHMARK_TEMPLATE_REGISTER_LIGHT(METHOD_NAME); \
	BENCHMARK_TEMPLATE_REGISTER_STL(METHOD_NAME)

BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(InsertRandValue);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(InsertRandValueReserve);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(FindRandValue);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(FindRandValueWithMisses);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(FindRandByKey);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(SequenceIteration);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(InsertAfterErase);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(FindAfterErase);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(InsertOrFind);
BENCHMARK_TEMPLATE_REGISTER_FOR_ALL_IMPLS(DeleteRandValue);

BENCHMARK_MAIN();

#include "debug_warning.h"
