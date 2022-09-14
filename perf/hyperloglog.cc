#include "salad/hll.h"
#include "salad/hll_empirical.h"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <vector>
#include <algorithm>

static constexpr int64_t
n_registers(int prec)
{
	return INT64_C(1) << prec;
}

uint64_t
big_rand()
{
	uint64_t r1 = std::rand();
	uint64_t r2 = std::rand();
	uint64_t r3 = std::rand();
	return r1 * r2 * r3;
}

using hash_array_t = std::vector<uint64_t>;

hash_array_t
rand_hash_array(size_t size)
{
	hash_array_t hashes = hash_array_t(size);
	auto hasher = std::hash<double>{};
	std::generate(hashes.begin(), hashes.end(),
		      [hasher] { return hasher(big_rand()); });
	return hashes;
}

void
hll_add_hashes(struct hll *hll, const hash_array_t &hashes)
{
	for (auto hash : hashes)
		hll_add(hll, hash);
}

/*
 * Cardinality for which the HyperLogLog algorithm is always used.
 * It can be any cardinality that exceeds the thresholds of using the
 * LinearCouinting algorithm.
 */
static constexpr int64_t
big_card(int prec)
{
	return 3 * n_registers(prec);
}

static void
initialize_dense_bench_arguments(benchmark::internal::Benchmark *bench)
{
	for (int prec = HLL_MIN_PRECISION; prec <= HLL_MAX_PRECISION; ++prec)
		bench->Args({prec, big_card(prec)});
}

void
bench_dense_hll_adding(benchmark::State &state)
{
	const auto prec = state.range(0);
	const auto card = state.range(1);

	hash_array_t hashes;
	for (auto _ : state) {
		state.PauseTiming();
		hashes = rand_hash_array(card);
		struct hll *hll = hll_new(prec, HLL_DENSE);
		state.ResumeTiming();

		hll_add_hashes(hll, hashes);

		hll_delete(hll);
	}
}

BENCHMARK(bench_dense_hll_adding)->
	Apply(initialize_dense_bench_arguments);

void
bench_dense_hll_estimating(benchmark::State &state)
{
	const auto prec = state.range(0);
	const auto card = state.range(1);

	hash_array_t hashes;
	for (auto _ : state) {
		state.PauseTiming();
		struct hll *hll = hll_new(prec, HLL_DENSE);
		hashes = rand_hash_array(card);
		hll_add_hashes(hll, hashes);
		state.ResumeTiming();

		benchmark::DoNotOptimize(hll_estimate(hll));

		hll_delete(hll);
	}
}

BENCHMARK(bench_dense_hll_estimating)->
	Apply(initialize_dense_bench_arguments);

static void
initialize_sparse_bench_arguments(benchmark::internal::Benchmark *bench)
{
	/*
	 * Sparse representation stores 4-byte pairs instead of 6-bit registers,
	 * so 32/6 < 6 times fewer pairs can be stored in the same amount of
	 * memory.
	 */
	for (int prec = HLL_MIN_PRECISION; prec <= HLL_MAX_PRECISION; ++prec)
		bench->Args({prec, n_registers(prec) / 6});
}

void
bench_sparse_hll_adding(benchmark::State &state)
{
	const auto prec = state.range(0);
	const auto card = state.range(1);

	hash_array_t hashes;
	for (auto _ : state) {
		state.PauseTiming();
		hashes = rand_hash_array(card);
		struct hll *hll = hll_new(prec, HLL_SPARSE);
		state.ResumeTiming();

		hll_add_hashes(hll, hashes);

		hll_delete(hll);
	}
}

BENCHMARK(bench_sparse_hll_adding)->
	Apply(initialize_sparse_bench_arguments);

void
bench_sparse_hll_estimating(benchmark::State &state)
{
	const auto prec = state.range(0);
	const size_t card = state.range(1);

	hash_array_t hashes;
	for (auto _ : state) {
		state.PauseTiming();
		hashes = rand_hash_array(card);
		struct hll *hll = hll_new(prec, HLL_SPARSE);
		hll_add_hashes(hll, hashes);
		state.ResumeTiming();

		benchmark::DoNotOptimize(hll_estimate(hll));

		hll_delete(hll);
	}
}

BENCHMARK(bench_sparse_hll_estimating)->
	Apply(initialize_sparse_bench_arguments);

BENCHMARK_MAIN();
