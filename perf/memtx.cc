#include <cstring>
#include <random>
#include <vector>

#include "box/allocator.h"
#include "box/box.h"
#include "box/index_def.h"
#include "box/iproto_constants.h"
#include "box/memtx_allocator.h"
#include "box/memtx_engine.h"
#include "box/memtx_space.h"
#include "box/memtx_tx.h"
#include "box/port.h"
#include "box/session.h"
#include "box/space_cache.h"
#include "box/space_def.h"
#include "box/tuple.h"
#include "box/txn.h"
#include "box/user.h"

#include "core/event.h"
#include "core/fiber.h"
#include "core/memory.h"

#include "lua/init.h"

extern "C" {
#include <lauxlib.h>
}

#include "rmean.h"

#include <benchmark/benchmark.h>

/**
 * This suite contains benchmarks for memtx - Tarantool's in-memory storage
 * engine.
 *
 * This suite uses the engine with 1 fiber and with WAL turned off (using a
 * temporary space), so only the CPU and the cache are exercised.
 */

/**
 * The engine can be tuned using the options below:
 */
static constexpr std::uint64_t memtx_tuple_arena_max_size = 1ull << 30;
static constexpr std::uint32_t memtx_objsize_min = 16;
static constexpr unsigned int memtx_granularity = 8;
static constexpr float memtx_alloc_factor = 1.1;

/**
 * Configuration of the space and its indexes can be tweaked using the
 * parameters below:
 */
static constexpr std::uint32_t sid = 0; /** Space id. */
static constexpr std::uint32_t tree_index_id = 0;
static constexpr std::uint32_t primary_index_id = tree_index_id;

/**
 * The dataset is generated using the methods of the memtx fixture (
 * `MemtxFixure`). It consists of {uint} tuples.
 *
 * The dataset can be tweaked using the parameters below:
 */
/** Parameters of the keys that form the dataset. */
static constexpr std::size_t key_size_max = 5 + 9;
/** Parameters of the key set used for benchmarking `select`. */
static constexpr std::size_t key_set_size = 1 << 17;
/** Parameters of the key subset used for benchmarking `get`. */
static constexpr std::size_t key_subset_size = key_set_size / 2;

/**
 * The memtx singleton encapsulates responsibility for initialization of
 * everything related to memtx: the engine, the subsystems it depends on, the
 * spaces and their indexes.
 */
class Memtx final {
public:
	Memtx(Memtx &other) = delete;
	Memtx &operator=(Memtx &other) = delete;

	static Memtx &
	instance()
	{
		static Memtx singleton;
		return singleton;
	}

private:
	Memtx()
	{
		::memory_init();
		::fiber_init(fiber_c_invoke);
		::memtx_tx_manager_init();
		::event_init();

		::txn_event_trigger_init();
		::space_cache_init();
		::user_cache_init();
		::session_init();
		::tuple_init(nullptr);

		/*
		 * The fiber slice settings need to be tweaked, since this
		 * benchmark uses only one fiber.
		 */
		cord()->max_slice.err = TIMEOUT_INFINITY;
		cord()->slice.err = TIMEOUT_INFINITY;

		/* Needed for `cord_on_yield` to work. */
		tarantool_L = luaL_newstate();
		if (tarantool_L == nullptr)
			panic("failed to create new Lua state");

		rmean_box =
			::rmean_new(iproto_type_strs, IPROTO_TYPE_STAT_MAX);
		if (rmean_box == nullptr)
			panic("failed to create new rmean");
		rmean_error =
			::rmean_new(rmean_error_strings, RMEAN_ERROR_LAST);
		if (rmean_error == nullptr)
			panic("failed to create new rmean");

		memtx = ::memtx_engine_new(".", /*force_recovery=*/true,
					   memtx_tuple_arena_max_size,
					   memtx_objsize_min,
					   /*dontdump=*/true,
					   memtx_granularity, "small",
					   memtx_alloc_factor,
					   /*threads_num=*/0,
					   memtx_on_indexes_built_mock_cb);
		if (memtx == nullptr)
			panic("failed to create new memtx engine");

		/* Skip recovery. */
		memtx->state = MEMTX_OK;

		::port_init();

		struct space_opts space_opts;
		::space_opts_create(&space_opts);
		space_opts.group_id = GROUP_LOCAL;
		space_opts.type = SPACE_TYPE_TEMPORARY;
		space_def = ::space_def_new(sid, /*uid=*/0/*GUEST*/,
					    /*exact_field_count=*/0, "perf",
					    std::strlen("perf"), "memtx",
					    std::strlen("memtx"), &space_opts,
					    &field_def_default,
					    /*field_count=*/0,
					    /*format_data=*/nullptr,
					    /*format_data_len=*/0);
		if (space_def == nullptr)
			panic("failed to create new space definition");
		RLIST_HEAD(key_list);
		struct index_opts idx_opts;
		::index_opts_create(&idx_opts);
		std::uint32_t fields[] = { 0 };
		std::uint32_t types[] = { FIELD_TYPE_UNSIGNED };
		key_def = ::box_key_def_new(fields, types, 1);
		if (key_def == nullptr)
			panic("failed to create new key definition");
		tree_idx_def = ::index_def_new(sid, tree_index_id, "pk",
					       std::strlen("pk"), TREE,
					       &idx_opts, key_def,
					       /*pk_def=*/nullptr);
		if (tree_idx_def == nullptr)
			panic("failed to create new index definition");
		::index_def_list_add(&key_list, tree_idx_def);
		struct space *space =
			::memtx_space_new(memtx, space_def, &key_list);
		if (space == nullptr)
			panic("failed to create new space");
		::space_cache_replace(/*old_space=*/nullptr, space);
		if (::space_add_primary_key(space) != 0)
			panic("failed to add primary key");
	}

	~Memtx()
	{
		lua_close(tarantool_L);
		tarantool_L = NULL;

		::index_def_delete(tree_idx_def);
		::space_def_delete(space_def);
		::key_def_delete(key_def);

		::session_free();
		::user_cache_free();
		::space_cache_destroy();

		memtx->base.vtab->shutdown(&memtx->base);
		::tuple_free();
		::txn_event_trigger_free();

		::fiber_free();
		::memory_free();
	}

	static void
	(memtx_on_indexes_built_mock_cb)() {}

	/** Memtx state needed for cleanup. */
	struct memtx_engine *memtx{};
	struct space_def *space_def{};
	struct key_def *key_def{};
	struct index_def *tree_idx_def{};
};

/**
 * The memtx fixture encapsulates the generation of the main key set used for
 * benchmarking `select` and the key subset used for benchmarking `get`.
 */
class MemtxFixture : public benchmark::Fixture {
public:
	MemtxFixture() : key_set(key_set_size), key_subset(key_subset_size) {}

	void
	SetUp(const benchmark::State &) final
	{
		/* Initialize the memtx singleton. */
		Memtx::instance();

		generate_key_set();
		generate_key_subset();
		empty_key.second = ::mp_encode_array(empty_key.first, 0);
	}

	void
	TearDown(const benchmark::State &) final
	{
		remove_key_set();
	}

private:
	/**
	 * Generate a key set of {uint} tuples and insert them into the index.
	 */
	void
	generate_key_set()
	{
		for (std::size_t i = 0; i < key_set.size(); ++i) {
			key_set[i].second =
				::mp_encode_array(key_set[i].first, 1);
			key_set[i].second =
				::mp_encode_uint(key_set[i].second, i);
			if (::box_insert(sid, key_set[i].first,
					 key_set[i].second,
					 /*result=*/nullptr) != 0)
				panic("failed to insert key");
		}
	}

	/** Remove the key set by deleting all keys from the primary index. */
	void
	remove_key_set()
	{
		for (const auto &key : key_set) {
			if (::box_delete(sid, primary_index_id, key.first,
					 key.second, /*result=*/nullptr) != 0)
				panic("failed to delete key");
		}
	}

protected:
	/**
	 * Generate a key subset by choosing a random subset from the main key
	 * set.
	 */
	void
	generate_key_subset()
	{
		for (auto &k : key_subset) {
			auto &key = key_set[distr(rng) % key_set.size()];
			k = {key.first, key.second};
		}
	}

	/** Random generator used for generation of indexes for key subset. */
	std::mt19937 rng{std::random_device()()};
	std::uniform_int_distribution<std::mt19937::result_type> distr;

	/** The key set used for benchmarking `select`. */
	std::vector<std::pair<char[key_size_max], char *>> key_set;

	/** The key subset used for benchmarking `get`. */
	std::vector<std::pair<char *, char *>> key_subset;

	/** The auxiliary empty key facilitates calling `select(nil)`. */
	static constexpr std::uint32_t select_lim = 4294967295;
	static constexpr std::size_t empty_key_size_max = 5;
	std::pair<char[empty_key_size_max], char *> empty_key;
};

/**
 * Benchmark random `get`s of existing keys from the tree index. The key subset
 * is regenerated through every iteration. This benchmark is supposed to
 * exercise the CPU and the cache.
 */
BENCHMARK_F(MemtxFixture, TreeGetRandomExistingKeys)
(benchmark::State &state)
{
	auto itr = key_subset.begin();
	int64_t counter = 0;
	struct tuple *t;
	for (MAYBE_UNUSED auto _ : state) {
		if (itr == key_subset.end()) {
			state.PauseTiming();
			generate_key_subset();
			state.ResumeTiming();
			itr = key_subset.begin();
		}
		benchmark::DoNotOptimize(::box_index_get(
			sid, tree_index_id, itr->first,
			itr->second, &t));
		++counter;
		++itr;
	}
	state.SetItemsProcessed(counter);
}

/**
 * Benchmark a `get` of one random exist key from the tree index. This benchmark
 * is supposed to exercise the CPU.
 */
BENCHMARK_F(MemtxFixture, TreeGet1RandomExistingKey)
(benchmark::State &state)
{
	int64_t counter = 0;
	struct tuple *t;
	const auto &k = key_set[distr(rng) % key_set.size()];
	for (MAYBE_UNUSED auto _ : state) {
		benchmark::DoNotOptimize(::box_index_get(sid, tree_index_id,
							 k.first, k.second,
							 &t));
		++counter;
	}
	state.SetItemsProcessed(counter);
}

/**
 * Benchmark `select` of all keys from the tree index. This benchmark is
 * supposed to exercise the CPU and the cache.
 */
BENCHMARK_F(MemtxFixture, TreeSelectAll)
(benchmark::State &state)
{
	int64_t counter = 0;
	struct port port;
	const char *packed_pos = NULL;
	const char *packed_pos_end = NULL;
	for (MAYBE_UNUSED auto _ : state) {
		benchmark::DoNotOptimize(::box_select(
			sid, tree_index_id, ITER_ALL, 0, select_lim,
			empty_key.first, empty_key.second, &packed_pos,
			&packed_pos_end, /*update_pos=*/false, &port));
		counter += key_set_size;
		state.PauseTiming();
		::port_destroy(&port);
		state.ResumeTiming();
	}
	state.SetItemsProcessed(counter);
}

/**
 * Benchmark random `replace`s of existing keys in the tree index.
 * The key subset is regenerated through every iteration.
 */
BENCHMARK_F(MemtxFixture, TreeReplaceRandomExistingKeys)
(benchmark::State & state)
{
	auto itr = key_subset.begin();
	int64_t counter = 0;
	for (MAYBE_UNUSED auto _ : state) {
		if (itr == key_subset.end()) {
			state.PauseTiming();
			generate_key_subset();
			state.ResumeTiming();
			itr = key_subset.begin();
		}
		struct tuple *result;
		if (::box_replace(sid, itr->first, itr->second, &result) != 0)
			panic("failed to replace the tuple");
		benchmark::DoNotOptimize(result);
		++counter;
		++itr;
	}
	state.SetItemsProcessed(counter);
}

BENCHMARK_MAIN();

#include "debug_warning.h"
