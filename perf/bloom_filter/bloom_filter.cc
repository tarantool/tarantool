#include <iostream>
#include <fstream>
#include <vector>
#include <string>

#include "bloom_filter.h"
#include "msgpuck.h"
#include "random.h"
#include "trivia/util.h" /* For xmalloc. */

#include "../utils/timer.h"

/** Primary key id. */
static constexpr std::uint32_t
PK_IID = 0;
/** Count of processed tuples.
 *    2^14 -- OK time;
 *    2^17 -- LONG time;
 *    2^18 -- OK.
 */
/** Count of tuples for box insert warmup. */
static constexpr std::uint32_t
TUPLE_WARMUP_COUNT = 1 << 5;
/**
 * Size of random string to be inserted inside space.
 * It has to be big enough so the dump occurs more often.
 */
static constexpr std::uint32_t
STRING_FIELD_SIZE = 10000;
/** Number of fields in inserted tuples. */
static constexpr std::uint32_t
TUPLE_FIELD_COUNT = 2;
/** Tuple string for filling space. */
static char tuple_string[STRING_FIELD_SIZE];
/** File to save benchmark results. */
static std::ofstream resultsFile;
/** File name. */
static std::string resultsFileName = "results.yml";

/** Represents one tuple. */
struct Tuple {
	char *begin;
	char *end;

	char *index_begin;
	char *index_end;

	/** Generate tuple with following format: {uint, string}, where
	 *  string (aka second field) is a string of size STRING_FIELD_SIZE.
	 */
	explicit Tuple(std::uint32_t pk_value)
	{
		std::size_t tuple_size =
			mp_sizeof_array(TUPLE_FIELD_COUNT) +
			mp_sizeof_uint(pk_value) +
			mp_sizeof_str(STRING_FIELD_SIZE);
		begin = (char *)xmalloc(tuple_size);
		end = mp_encode_array(begin, TUPLE_FIELD_COUNT);
		end = mp_encode_uint(end, pk_value);
		end = mp_encode_str(end, (const char *)tuple_string,
				    STRING_FIELD_SIZE);
		assert(end == begin + tuple_size);

		std::size_t key_size =
			mp_sizeof_array(1) +
			mp_sizeof_uint(pk_value);
		index_begin = (char *)xmalloc(key_size);
		index_end = mp_encode_array(index_begin, 1);
		index_end = mp_encode_uint(index_end, pk_value);
		assert(index_end == index_begin + key_size);
	}
};

/** Represents storage for tuples. */
struct TupleData {
	explicit TupleData(std::size_t tuple_count)
	{
		selectTuples.reserve(tuple_count);
		insertTuples.reserve(tuple_count);

		std::cout << "Started filling tupleData vectors." << std::endl;
		for (std::uint32_t i = 0; i < tuple_count; ++i) {
			Tuple newTuple(i);
			insertTuples.push_back(newTuple);
			selectTuples.push_back(newTuple);
		}
		std::cout << "Finished filling tupleData vectors." << std::endl;
		std::cout << std::endl;
	}

	TupleData(const TupleData &) = delete;

	TupleData(TupleData &&) = delete;

	~TupleData() {
		for (auto & t : selectTuples)
			free(t.begin);
	}

	std::vector<Tuple> selectTuples;
	std::vector<Tuple> insertTuples;
};

/** Tuple that will make select query look up all tree. */
Tuple *nonexistenetTuple;
/** Struct that holds:
 *  1. inserted tuples;
 *  2. tuples for select queries.
 */
TupleData *data;

/** Args passed through lua code. */
struct BenchArgs {
	/** Name of benchmarked space. */
	const char *space_name;
	/** Space id. */
	uint32_t space_id;
	/** Tuple count. */
	uint32_t tuple_count;

	explicit BenchArgs(const char *args, const char *args_end) {
		uint32_t arg_count = mp_decode_array(&args);
		uint32_t name_len = 0;
		const char *name = mp_decode_str(&args, &name_len);
		space_name = strndup(name, name_len);
		space_id = mp_decode_uint(&args);
		tuple_count = mp_decode_uint(&args);
	}

	BenchArgs(const BenchArgs &) = delete;

	BenchArgs(BenchArgs &&other) = delete;

	~BenchArgs() { free((void *)space_name); }
};

/** Trivial wrapper for storing benchmark execution time. */
struct BenchTimeResult {
	double elapsed_time;
};

/** Represents results of benchmark. */
struct BenchAlgorithmResult {
	BenchAlgorithmResult(const char *name) : space_name(name) {}

	/** Name of benchmarking space. */
	const char *space_name;
	/** Primary index bloom filter binary size. */
	size_t bloom_bsize = 0;
	struct BenchTimeResult insertTimeMs;
	struct BenchTimeResult selectTimeMs;
};

int
init(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	uint32_t arg_count = mp_decode_array(&args);
	uint32_t name_len = 0;
	const char *name = mp_decode_str(&args, &name_len);
	const char *build_name = strndup(name, name_len);
	uint32_t tuple_count = mp_decode_uint(&args);

	(void)ctx;
	(void)args;
	(void)args_end;

	std::cout << std::endl;
	std::cout << "-----------------------------------------------------------------" << std::endl;
	std::cout << "STARTED benchmarking." << std::endl;

	memset(tuple_string, '.', STRING_FIELD_SIZE);
	nonexistenetTuple = new Tuple(tuple_count + 1);
	data = new TupleData(tuple_count);
	resultsFile.open(resultsFileName, std::ios_base::in);
	if (!resultsFile) {
		std::cout << "Results file "
			  << resultsFileName
			  << " does not exist. Creating it." << std::endl;
		resultsFile.open(resultsFileName, std::ios_base::app);
		resultsFile << "builds:" << std::endl;
	} else {
		std::cout << "Results file "
			  << resultsFileName
			  << " does exist. Starting to write." << std::endl;
		resultsFile.close();
		resultsFile.open(resultsFileName, std::ios_base::app);
	}
	resultsFile << "    - build:" << std::endl;
	resultsFile << "            build_name: " << build_name << std::endl;
	resultsFile << "            tuple_count: " << tuple_count << std::endl;
	resultsFile << "            spaces:" << std::endl;
	return 0;
}

int
stop(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)ctx;
	(void)args;
	(void)args_end;

	delete data;
	delete nonexistenetTuple;
	resultsFile.close();

	std::cout << "FINISHED benchmarking." << std::endl;
	std::cout << "-----------------------------------------------------------------" << std::endl;
	std::cout << std::endl;
	return 0;
}

static void
print_result(uint32_t tuple_count, const struct BenchAlgorithmResult &result)
{
	resultsFile.precision(1);
	resultsFile << "                    - space:" << std::endl;
	resultsFile << "                            space_name: " << std::fixed
		    << result.space_name << std::endl;
	resultsFile << "                            insert: "
		    << 1000 * tuple_count / result.insertTimeMs.elapsed_time << std::endl;
	resultsFile << "                            select: "
		    << 1000 * tuple_count / result.selectTimeMs.elapsed_time << std::endl;
	resultsFile << "                            bloom_size: "
		    << result.bloom_bsize << std::endl;
	std::cout << "Written new result for " << result.space_name
		  << " space to the disk." << std::endl;
	std::cout << std::endl;
}

/**
 * Function that fills data for future queries.
 * Also warms up the hole db.
 */
static void bench_setup(uint32_t space_id, TupleData *data, BenchAlgorithmResult &result)
{
	PerfTimer timer{};
	std::cout << "Started inserting tuples into "
		  << result.space_name << " space." << std::endl;
	timer.start();

	for (const auto & t : data->insertTuples) {
		box_insert(space_id, t.begin, t.end, NULL);
	}
	timer.stop();

	result.insertTimeMs = BenchTimeResult{timer.elapsed_ms()};
	std::cout << "Finished inserting tuples into "
	          << result.space_name << " space." << std::endl;
}

static void
bench_select(uint32_t space_id, int index_id, TupleData *data,
			 BenchAlgorithmResult &result)
{
	PerfTimer timer{};
	timer.start();
	for (const auto & t: data->selectTuples) {
		box_tuple_t *tuple;
		int res = box_index_get(space_id, index_id, t.index_begin, t.index_end, &tuple);
		if (res == -1) {
			std::cout << "Error occurred while executing select query." << std::endl;
			exit(1);
		}
	}
	timer.stop();
	result.selectTimeMs = BenchTimeResult{timer.elapsed_ms()};
}


/** Replace a few entries to space to make sure that all possibly deferred
 *  things are setup (like <WHAT?>).
 */
static void
warmup(uint32_t space_id, TupleData *data)
{
	for (const auto & t : data->insertTuples) {
		box_insert(space_id, t.index_begin, t.index_end, NULL);
	}
	box_truncate(space_id);
}

int
run(box_function_ctx_t *ctx, const char *args, const char *args_end)
{
	(void)ctx;
	BenchArgs benchArgs{args, args_end};

	BenchAlgorithmResult results(benchArgs.space_name);

	warmup(benchArgs.space_id, data);
	bench_setup(benchArgs.space_id, data, results);
	bench_select(benchArgs.space_id, PK_IID, data, results);
	box_index_bloom_bsize(benchArgs.space_id, PK_IID, &results.bloom_bsize);
	print_result(benchArgs.tuple_count, results);

	return 0;
}
