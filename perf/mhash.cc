#include <algorithm>
#include <array>
#include <cstdlib>
#include <numeric>
#include <vector>

#include <benchmark/benchmark.h>

#define mh_name _u64
#define mh_key_t uint64_t
#define mh_node_t uint64_t
#define mh_arg_t void *
#define mh_hash(a, arg) (*(a))
#define mh_hash_key(a, arg) (a)
#define mh_cmp(a, b, arg) (*(a) != *(b))
#define mh_cmp_key(a, b, arg) ((a) != *(b))
#define MH_SOURCE
#include <salad/mhash.h>

class MHashU64Fixture : public benchmark::Fixture {
public:
	MHashU64Fixture() : h{nullptr}, rand_keys{}, filler_keys(max_key_count)
	{
		std::srand(0);
		generate_rand_absent_keys();

		std::iota(filler_keys.begin(), filler_keys.end(), 0);
	}

	void
	SetUp(const benchmark::State &state) final
	{
		h = ::mh_u64_new();
		for (std::int64_t i = 0; i < state.range(0); ++i)
			::mh_u64_put(h, &filler_keys[i], nullptr, nullptr);
	}

	void
	TearDown(__attribute__((__unused__)) const benchmark::State &state) final
	{
		::mh_u64_delete(h);
	}

	static constexpr std::size_t max_key_count = 1 << 20;

protected:
	mh_u64_t *h;

	static constexpr std::size_t rand_keys_count = 1 << 10;
	std::array<std::uint64_t, rand_keys_count> rand_keys;

	std::vector<std::uint64_t> filler_keys;

	void
	generate_rand_absent_keys()
	{
		std::generate(rand_keys.begin(), rand_keys.end(), []() {
			/* [max_key_count; UINT64_MAX) */
			return max_key_count +
			       std::rand() % (UINT64_MAX - max_key_count);
		});
	}
};

BENCHMARK_DEFINE_F(MHashU64Fixture, FindRandAbsentKey)
(benchmark::State &state)
{
	std::int64_t iterations_count = 0;
	for (__attribute__((__unused__)) auto _: state) {
		for (auto &k: rand_keys) {
			++iterations_count;
			benchmark::DoNotOptimize(::mh_u64_find(h, k, nullptr));
		}
		state.PauseTiming();
		generate_rand_absent_keys();
		state.ResumeTiming();
	}
	state.SetItemsProcessed(iterations_count);
}

BENCHMARK_REGISTER_F(MHashU64Fixture, FindRandAbsentKey)->RangeMultiplier(2)->Range(1, MHashU64Fixture::max_key_count);

BENCHMARK_MAIN();
