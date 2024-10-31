#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>
#include <time.h>
#include <random>
#include <memory>

#include <benchmark/benchmark.h>
#include <trivia/util.h>

#define BPS_TREE_NO_DEBUG 1

/* A simple test tree. */

#define tree_i64_EXTENT_SIZE 8192
#define tree_i64_elem_t int64_t
#define tree_i64_key_t int64_t
#define BPS_TREE_NAME tree_i64_t
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE tree_i64_EXTENT_SIZE
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) ((a) - (b))
#define BPS_TREE_COMPARE_KEY(a, b, arg) ((a) - (b))
#define bps_tree_elem_t tree_i64_elem_t
#define bps_tree_key_t tree_i64_key_t
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t

/* Tree with child cardinalities of inner blocks. */

#define BPS_INNER_CHILD_CARDS
#define treecc_i64_EXTENT_SIZE 8192
#define treecc_i64_elem_t int64_t
#define treecc_i64_key_t int64_t
#define BPS_TREE_NAME treecc_i64_t
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE treecc_i64_EXTENT_SIZE
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) ((a) - (b))
#define BPS_TREE_COMPARE_KEY(a, b, arg) ((a) - (b))
#define bps_tree_elem_t treecc_i64_elem_t
#define bps_tree_key_t treecc_i64_key_t
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef BPS_INNER_CHILD_CARDS

/* Tree with inner block cardinalities. */

#define BPS_INNER_CARD
#define treeic_i64_EXTENT_SIZE 8192
#define treeic_i64_elem_t int64_t
#define treeic_i64_key_t int64_t
#define BPS_TREE_NAME treeic_i64_t
#define BPS_TREE_BLOCK_SIZE 512
#define BPS_TREE_EXTENT_SIZE treeic_i64_EXTENT_SIZE
#define BPS_TREE_IS_IDENTICAL(a, b) ((a) == (b))
#define BPS_TREE_COMPARE(a, b, arg) ((a) - (b))
#define BPS_TREE_COMPARE_KEY(a, b, arg) ((a) - (b))
#define bps_tree_elem_t treeic_i64_elem_t
#define bps_tree_key_t treeic_i64_key_t
#include "salad/bps_tree.h"
#undef BPS_TREE_NAME
#undef BPS_TREE_BLOCK_SIZE
#undef BPS_TREE_EXTENT_SIZE
#undef BPS_TREE_IS_IDENTICAL
#undef BPS_TREE_COMPARE
#undef BPS_TREE_COMPARE_KEY
#undef bps_tree_elem_t
#undef bps_tree_key_t
#undef BPS_INNER_CARD

/**
 * Generate the benchmark variations required.
 */

/* Run the func benchmark on the tree of the given size. */
#define generate_benchmark_size(type, func, size) static void \
	type##_##func##_size_##size(benchmark::State &state) \
	{ \
		test_##func<type>(state, size); \
		state.SetItemsProcessed(size); \
	} \
	BENCHMARK(type##_##func##_size_##size)

/* Run the func benchmark on the tree of the given height (max size). */
#define generate_benchmark_height(type, func, height) static void \
	type##_##func##_height_##height(benchmark::State &state) \
	{ \
		test_##func<type>(state, type::height_##height##_max_size); \
		state.SetItemsProcessed(type::height_##height##_max_size); \
	} \
	BENCHMARK(type##_##func##_height_##height)

/* The same as generate_benchmark_size, but sets iterations explicitly. */
#define generate_benchmark_size_iterations(type, func, size) \
	generate_benchmark_size(type, func, size)->Iterations(size)

/* Meant to create specified benchmarks for all trees. */
#define generate_benchmarks(generator, func, arg) \
	generator(tree_i64, func, arg); \
	generator(treecc_i64, func, arg); \
	generator(treeic_i64, func, arg)

/* Create size-based benchmarks for all trees. */
#define generate_benchmarks_size(func, size) \
	generate_benchmarks(generate_benchmark_size, func, size)

/* Create height-based benchmarks for all trees. */
#define generate_benchmarks_height(func, height) \
	generate_benchmarks(generate_benchmark_height, func, height)

/* Create size-based benchmarks with explicit iteration count for all trees. */
#define generate_benchmarks_size_iterations(func, size) \
	generate_benchmarks(generate_benchmark_size_iterations, func, size)

/**
 * Regular allocators (like small or malloc) contain complicated logic, which
 * is a source of noise for benchmarks. Allocator performance can degrade when
 * some conditions are met, allocation of blocks can consume variable amount
 * of time and so on.
 *
 * To mitigate this let's introduce a very simple region-like allocator.
 */
template<int EXTENT_SIZE>
struct DummyAllocator {
	std::unique_ptr<char[]> m_buf;
	size_t m_buf_size;
	size_t m_pos = 0;
	struct matras_allocator matras_allocator;

	DummyAllocator(size_t size)
	{
		matras_allocator_create(&matras_allocator,
					EXTENT_SIZE, alloc, free);
		m_buf_size = ((size + EXTENT_SIZE - 1) / EXTENT_SIZE) *
			     EXTENT_SIZE;
		/* The calculated size is incorrect for small trees. */
		if (m_buf_size < EXTENT_SIZE * 10)
			m_buf_size = EXTENT_SIZE * 10;
		m_buf = std::unique_ptr<char[]>(new char[m_buf_size]);
	}

	~DummyAllocator()
	{
		matras_allocator_destroy(&matras_allocator);
	}

	void
	reset()
	{
		m_pos = 0;
	}

	static void *
	alloc(struct matras_allocator *allocator)
	{
		DummyAllocator *self = container_of(allocator, DummyAllocator,
						    matras_allocator);
		void *result = (void *)&self->m_buf[self->m_pos];
		self->m_pos += EXTENT_SIZE;
		if (unlikely(self->m_pos > self->m_buf_size)) {
			fprintf(stderr, "Ouf of bounds allocation.\n");
			exit(-1);
		}
		return result;
	}

	static void
	free(struct matras_allocator *allocator, void *extent)
	{
		(void)allocator;
		(void)extent;
	}
};

/**
 * Each tree configuration (tree_i64, tree_s128, etc.) has a class associated.
 * This makes it possible to create generic benchmarks using C++ templates.
 */
#define CREATE_TREE_CLASS(tree) \
class tree { \
public: \
	using tree_t = tree##_t; \
	using elem_t = tree##_elem_t; \
	using key_t = tree##_key_t; \
	using Allocator_Base = \
		DummyAllocator<tree##_EXTENT_SIZE>; \
	struct Allocator: public Allocator_Base { \
		Allocator(size_t count) \
		: Allocator_Base(count * sizeof(elem_t) * 2) \
		{} \
	}; \
	static constexpr size_t max_count_in_leaf = \
		BPS_TREE_##tree##_t_MAX_COUNT_IN_LEAF; \
	static constexpr size_t max_count_in_inner = \
		BPS_TREE_##tree##_t_MAX_COUNT_IN_INNER; \
	static constexpr size_t height_1_max_size = max_count_in_leaf; \
	static constexpr size_t height_2_max_size = height_1_max_size * \
						    max_count_in_inner; \
	static constexpr size_t height_3_max_size = height_2_max_size * \
						    max_count_in_inner; \
	static constexpr size_t height_4_max_size = height_3_max_size * \
						    max_count_in_inner; \
	static constexpr size_t height_5_max_size = height_4_max_size * \
						    max_count_in_inner; \
	static constexpr auto create = ::tree##_t_create; \
	static constexpr auto build = ::tree##_t_build; \
	static constexpr auto destroy = ::tree##_t_destroy; \
	static constexpr auto find = ::tree##_t_find; \
	static constexpr auto insert = ::tree##_t_insert; \
	static constexpr auto delete_ = ::tree##_t_delete; \
}

/* The class must be created for each instantiated BPS tree to test it. */
CREATE_TREE_CLASS(tree_i64);
CREATE_TREE_CLASS(treecc_i64);
CREATE_TREE_CLASS(treeic_i64);

/**
 * Value generators to make key-independent benchmarks.
 */

class ValueKey {
	size_t value;
public:
	ValueKey(size_t value) : value(value) {}

	size_t
	operator()()
	{
		return value;
	}
};

class RandomKey {
	size_t mod;
	std::minstd_rand rng;
public:
	RandomKey(size_t mod) : mod(mod) {}

	size_t
	operator()()
	{
		return rng() % mod;
	}
};

class IncrementingKey {
	size_t end;
	size_t value;
public:
	IncrementingKey(size_t end) : end(end), value(0) {}

	size_t
	operator()()
	{
		size_t result = value++;
		if (value == end)
			value = 0;
		return result;
	}
};

class DecrementingKey {
	size_t end;
	size_t value;
public:
	DecrementingKey(size_t end) : end(end), value(end) {}

	size_t
	operator()()
	{
		size_t result = --value;
		if (value == 0)
			value = end;
		return result;
	}
};

/**
 * Utility functions.
 */

template<class tree>
static std::unique_ptr<typename tree::elem_t[]>
sorted_elems(size_t size)
{
	auto arr = std::unique_ptr<typename tree::elem_t[]>(
		new typename tree::elem_t[size]);
	for (size_t i = 0; i < size; i++)
		arr[i] = i;
	return arr;
}

template<class tree>
static void
create(typename tree::tree_t &t, size_t size,
       typename tree::Allocator &allocator)
{
	auto arr = sorted_elems<tree>(size);
	tree::create(&t, 0, &allocator.matras_allocator, NULL);
	if (tree::build(&t, &arr[0], size) == -1) {
		fprintf(stderr, "Tree build has failed.\n");
		exit(-1);
	}
}

/**
 * The benchmarks.
 */

template<class tree>
static void
test_build(benchmark::State &state, size_t count)
{
	auto arr = sorted_elems<tree>(count);
	typename tree::Allocator allocator(count);
	for (auto _ : state) {
		typename tree::tree_t t;
		tree::create(&t, 0, &allocator.matras_allocator, NULL);
		tree::build(&t, &arr[0], count);
		tree::destroy(&t);
		allocator.reset();
	}
}

generate_benchmarks_size(build, 1000000);

template<class tree, class KeyGen>
static void
test_find(benchmark::State &state, size_t count, KeyGen kg)
{
	typename tree::tree_t t;
	typename tree::Allocator allocator(count);
	create<tree>(t, count, allocator);
	for (auto _ : state)
		benchmark::DoNotOptimize(tree::find(&t, kg()));
	tree::destroy(&t);
}

template<class tree>
static void
test_find_first(benchmark::State &state, size_t count)
{
	test_find<tree>(state, count, ValueKey(0));
}

generate_benchmarks_size(find_first, 1000000);

generate_benchmarks_height(find_first, 1);
generate_benchmarks_height(find_first, 2);
generate_benchmarks_height(find_first, 3);
generate_benchmarks_height(find_first, 4);

template<class tree>
static void
test_find_last(benchmark::State &state, size_t count)
{
	test_find<tree>(state, count, ValueKey(count - 1));
}

generate_benchmarks_size(find_last, 1000000);

generate_benchmarks_height(find_last, 1);
generate_benchmarks_height(find_last, 2);
generate_benchmarks_height(find_last, 3);
generate_benchmarks_height(find_last, 4);

template<class tree>
static void
test_find_inc(benchmark::State &state, size_t count)
{
	test_find<tree>(state, count, IncrementingKey(count));
}

generate_benchmarks_size(find_inc, 1000000);

generate_benchmarks_height(find_inc, 1);
generate_benchmarks_height(find_inc, 2);
generate_benchmarks_height(find_inc, 3);
generate_benchmarks_height(find_inc, 4);

template<class tree>
static void
test_find_dec(benchmark::State &state, size_t count)
{
	test_find<tree>(state, count, DecrementingKey(count));
}

generate_benchmarks_size(find_dec, 1000000);

generate_benchmarks_height(find_dec, 1);
generate_benchmarks_height(find_dec, 2);
generate_benchmarks_height(find_dec, 3);
generate_benchmarks_height(find_dec, 4);

template<class tree>
static void
test_find_rand(benchmark::State &state, size_t count)
{
	test_find<tree>(state, count, RandomKey(count));
}

generate_benchmarks_size(find_rand, 1000000);

generate_benchmarks_height(find_rand, 1);
generate_benchmarks_height(find_rand, 2);
generate_benchmarks_height(find_rand, 3);
generate_benchmarks_height(find_rand, 4);

/*
 * The following functions test performance of insertion and deletion without
 * reballancing. This is done by performing the two opposite operations in a
 * benchmark loop.
 */

template<class tree, class KeyGen>
static void
test_delete_insert(benchmark::State &state, size_t count, KeyGen kg)
{
	typename tree::tree_t t;
	typename tree::Allocator allocator(count);
	typename tree::elem_t replaced, successor;
	create<tree>(t, count, allocator);
	for (auto _ : state) {
		typename tree::elem_t elem(kg());
		tree::delete_(&t, elem);
		tree::insert(&t, elem, &replaced, &successor);
	}
	tree::destroy(&t);
}

template<class tree>
static void
test_delete_insert_first(benchmark::State &state, size_t count)
{
	test_delete_insert<tree>(state, count, ValueKey(0));
}

generate_benchmarks_size(delete_insert_first, 1000000);

generate_benchmarks_height(delete_insert_first, 1);
generate_benchmarks_height(delete_insert_first, 2);
generate_benchmarks_height(delete_insert_first, 3);
generate_benchmarks_height(delete_insert_first, 4);

template<class tree>
static void
test_delete_insert_last(benchmark::State &state, size_t count)
{
	test_delete_insert<tree>(state, count, ValueKey(count - 1));
}

generate_benchmarks_size(delete_insert_last, 1000000);

generate_benchmarks_height(delete_insert_last, 1);
generate_benchmarks_height(delete_insert_last, 2);
generate_benchmarks_height(delete_insert_last, 3);
generate_benchmarks_height(delete_insert_last, 4);

template<class tree>
static void
test_delete_insert_inc(benchmark::State &state, size_t count)
{
	test_delete_insert<tree>(state, count, IncrementingKey(count));
}

generate_benchmarks_size(delete_insert_inc, 1000000);

generate_benchmarks_height(delete_insert_inc, 1);
generate_benchmarks_height(delete_insert_inc, 2);
generate_benchmarks_height(delete_insert_inc, 3);
generate_benchmarks_height(delete_insert_inc, 4);

template<class tree>
static void
test_delete_insert_dec(benchmark::State &state, size_t count)
{
	test_delete_insert<tree>(state, count, DecrementingKey(count));
}

generate_benchmarks_size(delete_insert_dec, 1000000);

generate_benchmarks_height(delete_insert_dec, 1);
generate_benchmarks_height(delete_insert_dec, 2);
generate_benchmarks_height(delete_insert_dec, 3);
generate_benchmarks_height(delete_insert_dec, 4);

template<class tree>
static void
test_delete_insert_rand(benchmark::State &state, size_t count)
{
	test_delete_insert<tree>(state, count, RandomKey(count));
}

generate_benchmarks_size(delete_insert_rand, 1000000);

generate_benchmarks_height(delete_insert_rand, 1);
generate_benchmarks_height(delete_insert_rand, 2);
generate_benchmarks_height(delete_insert_rand, 3);
generate_benchmarks_height(delete_insert_rand, 4);

/*
 * The following functions test insertion and deletion routines including the
 * reballancing process overhead. The iteration count is specified explicitly
 * for insertion testing functions in order to allocate enough memory for the
 * test tree. The iteration count of deletion testing functions is specified
 * too in order to create trees big enough to perform max possible deletions.
 */

template<class tree, class KeyGen>
static void
test_insert(benchmark::State &state, size_t count, KeyGen kg)
{
	typename tree::Allocator allocator(count);

	typename tree::tree_t t;
	tree::create(&t, 0, &allocator.matras_allocator, NULL);
	typename tree::elem_t replaced, successor;
	for (auto _ : state)
		tree::insert(&t, kg(), &replaced, &successor);
	tree::destroy(&t);
}

template<class tree>
static void
test_insert_first(benchmark::State &state, size_t count)
{
	test_insert<tree>(state, count, DecrementingKey(count));
}

generate_benchmarks_size_iterations(insert_first, 1000000);

template<class tree>
static void
test_insert_last(benchmark::State &state, size_t count)
{
	test_insert<tree>(state, count, IncrementingKey(count));
}

generate_benchmarks_size_iterations(insert_last, 1000000);

template<class tree>
static void
test_insert_rand(benchmark::State &state, size_t count)
{
	test_insert<tree>(state, count, RandomKey(count));
}

generate_benchmarks_size_iterations(insert_rand, 1000000);

template<class tree, class KeyGen>
static void
test_delete(benchmark::State &state, size_t count, KeyGen kg)
{
	typename tree::tree_t t;
	typename tree::Allocator allocator(count);
	create<tree>(t, count, allocator);
	for (auto _ : state)
		tree::delete_(&t, kg());
	tree::destroy(&t);
}

template<class tree>
static void
test_delete_first(benchmark::State &state, size_t count)
{
	test_delete<tree>(state, count, IncrementingKey(count));
}

generate_benchmarks_size_iterations(delete_first, 1000000);

template<class tree>
static void
test_delete_last(benchmark::State &state, size_t count)
{
	test_delete<tree>(state, count, DecrementingKey(count));
}

generate_benchmarks_size_iterations(delete_last, 1000000);

template<class tree>
static void
test_delete_rand(benchmark::State &state, size_t count)
{
	test_delete<tree>(state, count, RandomKey(count));
}

generate_benchmarks_size_iterations(delete_rand, 1000000);

BENCHMARK_MAIN();

#include "debug_warning.h"
