#include <iostream>

#include "tuple_compression.h"
#include "msgpuck.h"

#include "../utils/randb.h"
#include "../utils/timer.h"

#ifndef lengthof
#define lengthof(array) (sizeof (array) / sizeof ((array)[0]))
#endif

// Primary key id.
static constexpr std::uint32_t PK_IID = 0;
// Count of processed tuples (both inserted and iterated).
static constexpr std::uint32_t TUPLE_COUNT = 1 << 15;
// Size of random (raw) string to be compressed inside tuple.
static constexpr std::uint32_t TUPLE_PAYLOAD_SIZE = 1 << 10;
static constexpr std::uint32_t TUPLE_FIELD_COUNT = 3;

// Means count of continuously repeating characters.
static constexpr double UNIQUE_SEQUENCE_RATIO[] = { 0.1, 0.01, 0.001 };
// Assume that strings consists of letters and digits only (26 + 26 + 10 = 62)
static constexpr unsigned CHAR_LOWER_BOUND = 0;
static constexpr unsigned CHAR_UPPER_BOUND = 62;

// Iteration over indexes is significantly faster than insertions, so to get
// millisecond timings let's run iterations several times.
static constexpr std::uint32_t SELECT_CYCLE_COUNT = 1 << 10;

static RandomBytesGenerator random_generator{};

struct TupleRaw {
	char *begin;
	char *end;

	// Generate tuple with following format: {uint, string, uint}, where
	// string (aka second field) is a string of size TUPLE_PAYLOAD_SIZE.
	explicit TupleRaw(std::uint32_t pk_value, double unique_ratio)
	{
		assert(unique_ratio <= 1.0 && unique_ratio >= 0.0);
		static unsigned char tmp_buf[TUPLE_PAYLOAD_SIZE];
		std::size_t repeat_count = TUPLE_PAYLOAD_SIZE * unique_ratio;
		char current_char = random_generator.get();
		std::size_t repeats = 0;
		for (std::size_t i = 0; i < TUPLE_PAYLOAD_SIZE; i++) {
			tmp_buf[i] = current_char;
			if (++repeats >= repeat_count) {
				repeats = 0;
				current_char = random_generator.get();
			}
		}
		std::size_t tuple_size =
			mp_sizeof_array(TUPLE_FIELD_COUNT) +
			mp_sizeof_uint(pk_value) +
			mp_sizeof_str(TUPLE_PAYLOAD_SIZE) +
			mp_sizeof_uint(pk_value);

		begin = (char *) malloc(tuple_size);
		end = mp_encode_array(begin, TUPLE_FIELD_COUNT);
		end = mp_encode_uint(end, pk_value);
		end = mp_encode_str(end, (const char *) tmp_buf, TUPLE_PAYLOAD_SIZE);
		end = mp_encode_uint(end, pk_value);
		assert(end == begin + tuple_size);
	}
};

struct TupleHolder {
	explicit TupleHolder(std::size_t tuple_count, double unique_ratio) : tuples()
	{
		tuples.reserve(tuple_count);
		for (std::uint32_t i = 0; i < tuple_count; ++i) {
			tuples.push_back(TupleRaw{i, unique_ratio});
		}
	}

	TupleHolder(const TupleHolder &) = delete;
	TupleHolder(TupleHolder &&) = delete;

	~TupleHolder()
	{
		for (auto &t : tuples)
			free(t.begin);
	}

	std::vector<TupleRaw> tuples;
};

struct BenchArgs {
	// Name of the algorithm to be benchmarked (zlib, lz4 etc); null terminated.
	const char *algorithm_name;
	// Space featuring @a algorithm_name compression.
	uint32_t space_id;

	// Args are passed as msgpack array [name, space_id]
	explicit BenchArgs(const char *args, const char *args_end)
	{
		uint32_t arg_count = mp_decode_array(&args);
		uint32_t name_len = 0;
		const char *name = mp_decode_str(&args, &name_len);
		algorithm_name = strndup(name, name_len);
		space_id = mp_decode_uint(&args);
	}
	BenchArgs(const BenchArgs &) = delete;
	BenchArgs(BenchArgs &&other) = delete;

	~BenchArgs() { free((void*)algorithm_name); }
};

// Trivial wrapper for storing benchmark execution time.
struct BenchResult {
	double elapsed_time;
};

struct BenchAlgorithmResult {
	explicit BenchAlgorithmResult(const char *name) : algorithm_name(name) { }
	// Name of tested algorithm.
	const char *algorithm_name;
	// Data uniqueness ratio.
	double unique_ratio;
	// Space data binary size.
	size_t bsize;
	struct BenchResult select;
	struct BenchResult replace;
};

int
print_header(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void) ctx;
	(void) args;
	(void) args_end;
	std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
	std::cout << "+   Tuple Count              " << TUPLE_COUNT << "bytes"           << std::endl;
	std::cout << "+   Tuple Payload Size       " << TUPLE_PAYLOAD_SIZE << "bytes"    << std::endl;
	std::cout << "+   Select cycle multiplier  " << SELECT_CYCLE_COUNT               << std::endl;
	std::cout << "+   Character values range   [" << CHAR_LOWER_BOUND << ", "
		<< CHAR_UPPER_BOUND << "]"                                               << std::endl;
	std::cout << "+   Schema                "                                        << std::endl;
	std::cout << "+      Format = { unsigned, string (compressed), unsigned }"       << std::endl;
	std::cout << "+      Primary key = { unsigned, string (compressed), unsigned }"  << std::endl;
	std::cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << std::endl;
	return 0;
}

static void
print_result(const struct BenchAlgorithmResult &result)
{
	std::cout.precision(3);
	std::cout << "+    Unique Ratio: " << std::fixed << result.unique_ratio          << std::endl;
	std::cout << "+    Bsize: " << result.bsize                                      << std::endl;
	std::cout << "+    REPLACE (elapsed): " << result.replace.elapsed_time << "ms"   << std::endl;
	std::cout << "+    SELECT  (elapsed): " << result.select.elapsed_time << "ms"    << std::endl;
	std::cout << "-----------------------------------------------------------------" << std::endl;
}

static void
print_algorithm_name(const char *name)
{
	std::cout << "================================================================="            << std::endl;
	std::cout << "+++++++++++++++++++++++ Algorithm " << name << " ++++++++++++++++++++++++++"   << std::endl;
}

static void
bench_replace(uint32_t space_id, BenchAlgorithmResult &result)
{
	TupleHolder data(TUPLE_COUNT, result.unique_ratio);
	PerfTimer timer{};
	timer.start();
	for (const auto &t : data.tuples)
		box_replace(space_id, t.begin, t.end, NULL);
	timer.stop();
	result.replace = BenchResult{ timer.elapsed_ms() };
}

static void
space_iterate_all(uint32_t space_id)
{
	char key[1];
	char *key_end = mp_encode_array(key, 0);

	box_tuple_t *tuple;
	box_iterator_t *it = box_index_iterator(space_id, PK_IID, ::ITER_ALL, key, key_end);
	volatile int x = 0;
	while (box_iterator_next(it, &tuple) == 0 && tuple != NULL) { x++; }
	box_iterator_free(it);
}

static void
bench_select(uint32_t space_id, BenchAlgorithmResult &result)
{
	PerfTimer timer{};
	timer.start();
	for (std::uint32_t i = 0; i < SELECT_CYCLE_COUNT; ++i)
		space_iterate_all(space_id);
	timer.stop();
	result.select = BenchResult{ timer.elapsed_ms() };
}

// Replace a few entries to space to make sure that all possibly deferred
// things are setup (like index extents are allocated etc).
static void
warmup(uint32_t space_id)
{
	TupleHolder data(128, 0.5);
	for (const auto &t : data.tuples)
		box_replace(space_id, t.begin, t.end, NULL);
	space_iterate_all(space_id);
	box_truncate(space_id);
}

int
run(box_function_ctx_t *ctx, const char *args_raw, const char *args_raw_end)
{
	(void) ctx;
	BenchArgs args{args_raw, args_raw_end};

	print_algorithm_name(args.algorithm_name);
	BenchAlgorithmResult results(args.algorithm_name);
	for (std::size_t i = 0; i < lengthof(UNIQUE_SEQUENCE_RATIO); ++i) {
		warmup(args.space_id);
		results.unique_ratio = UNIQUE_SEQUENCE_RATIO[i];
		bench_replace(args.space_id, results);
		bench_select(args.space_id, results);
		box_space_bsize(args.space_id, &results.bsize);
		print_result(results);
	}
	return 0;
}
