#include "salad/hll.h"

#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <functional>
#include <vector>
#include <algorithm>
#include <random>

static constexpr int64_t
n_registers(int prec)
{
	return INT64_C(1) << prec;
}

uint64_t
rand64()
{
	static std::random_device rd;
	static std::mt19937_64 gen(rd());
	static std::uniform_int_distribution<uint64_t> distr(0, UINT64_C(-1));
	return distr(gen);
}

std::vector<uint64_t>
rand_array(size_t size)
{
	std::vector<uint64_t> array(size);
	std::generate(array.begin(), array.end(), [] { return rand64(); });
	return array;
}

void
hll_add_hashes(struct hll *hll, const std::vector<uint64_t> &hashes)
{
	for (auto hash : hashes)
		hll_add(hll, hash);
}

/*
 * Cardinality for which the HyperLogLog algorithm is always used.
 * It can be any cardinality that exceeds the thresholds of using the
 * LinearCounting algorithm.
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

	std::vector<uint64_t> hashes;
	for (auto _ : state) {
		state.PauseTiming();
		hashes = rand_array(card);
		struct hll *hll = hll_new_concrete(prec, HLL_DENSE);
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

	std::vector<uint64_t> hashes;
	for (auto _ : state) {
		state.PauseTiming();
		struct hll *hll = hll_new_concrete(prec, HLL_DENSE);
		hashes = rand_array(card);
		hll_add_hashes(hll, hashes);
		state.ResumeTiming();

		benchmark::DoNotOptimize(hll_count_distinct(hll));

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

	std::vector<uint64_t> hashes;
	for (auto _ : state) {
		state.PauseTiming();
		hashes = rand_array(card);
		struct hll *hll = hll_new_concrete(prec, HLL_SPARSE);
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

	std::vector<uint64_t> hashes;
	for (auto _ : state) {
		state.PauseTiming();
		hashes = rand_array(card);
		struct hll *hll = hll_new_concrete(prec, HLL_SPARSE);
		hll_add_hashes(hll, hashes);
		state.ResumeTiming();

		benchmark::DoNotOptimize(hll_count_distinct(hll));

		hll_delete(hll);
	}
}

BENCHMARK(bench_sparse_hll_estimating)->
	Apply(initialize_sparse_bench_arguments);

BENCHMARK_MAIN();
