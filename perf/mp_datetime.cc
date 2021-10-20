/*
 * Copyright 2010-2020, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <iostream>
#include <tuple>

#include <msgpuck.h>

#include <benchmark/benchmark.h>

/**
 * The benchmark is based on binary search in a sorted array.
 */

/**
 * Number of dates in which a binary search will be executed.
 * For symmetry is the same number of dates that are searched in binary search.
 */
const size_t NUM_TEST_DATES = 4 * 1024;
/**
 * Imagine we have a big in-memory database with one space with dates in
 * each row. How many distinct timestamps will be there?
 * For estimation let's suppose that the range of several months. That
 * gives about 1e7 different timestamps.
 * But how common will be a situation when two rows have the same timestamp?
 * Let's suppose we have 10GB base with 100 bytes per row - 1e8 rows.
 * So on average every 10 rows will have the same timestamp (rounded to 16).
 * In this tests we should have the same ratio - there will be significantly
 * less rows, but every 10 (or 16) rows will have the same timestamp.
 */
const size_t DIFFERENT_TIMESTAMPS = NUM_TEST_DATES / 16;
/**
 * tzoffset range (+- this value).
 */
const size_t TZOFFSET_RANGE = 1024;
/**
 * tzindex range (from 0 to this value).
 */
const size_t TZINDEX_RANGE = 480;
/**
 * Means nothing, just a constant.
 */
const int8_t EXT_TYPE = 3;

/** Static data holder. */
char data1[32 * NUM_TEST_DATES];
char data2[32 * NUM_TEST_DATES];
const char* data1_ptrs[NUM_TEST_DATES];
const char* data2_ptrs[NUM_TEST_DATES];

/** Simple comparator for a group of values. */
template <class T>
int cmp_args(T a, T b)
{
	return a < b ? -1 : a > b;
}

template <class T, class... U>
int cmp_args(T a, T b, U...u)
{
	return a < b ? -1 : a > b ? 1 : cmp_args(u...);
}

/** The first dimension of the bench - data structure. */
/**
 * epoch is double.
 */
struct dbl_epoch {
	double epoch;
	uint32_t nsec;
	uint16_t tzoffset;
	uint16_t tzindex;

	friend int cmp(const dbl_epoch& a, const dbl_epoch& b) {
		return cmp_args(a.epoch, b.epoch, a.nsec, b.nsec,
				a.tzoffset, b.tzoffset, a.tzindex, b.tzindex);
	}
};

/**
 * epoch is integer.
 */
struct int_epoch {
	int64_t epoch;
	uint32_t nsec;
	uint16_t tzoffset;
	uint16_t tzindex;

	friend int cmp(const int_epoch& a, const int_epoch& b) {
		return cmp_args(a.epoch, b.epoch, a.nsec, b.nsec,
				a.tzoffset, b.tzoffset, a.tzindex, b.tzindex);
	}
};

/**
 * epoch is integer and the structure is reordered for faster comparison.
 */
struct reordered {
	uint16_t tzindex;
	uint16_t tzoffset;
	uint32_t nsec;
	int64_t epoch;

	friend int cmp(const reordered& a, const reordered& b) {
		__int128 x, y;
		static_assert(sizeof(x) == sizeof(a));
		memcpy(&x, &a, sizeof(x));
		memcpy(&y, &b, sizeof(y));
		return cmp_args(x, y);
	}
};

/** The second dimension of the bench - msgpack serialization format. */
enum encode_t {
	/** All members are msgpack encoded in MP_EXT data. */
	FMT_MP_FULL,
	/** Some (basically nonzero) members are mp encoded in MP_EXT data. */
	FMT_MP_PARTIAL,
	/** All the structure is directly copied to MP_EXT data. */
	FMT_RAW_FULL,
	/** Conditionally nonzero part of structure is copied to MP_EXT data. */
	FMT_RAW_PARTIAL,
};

/** The third dimension of the bench - datetime variety workload. */
enum workload_t {
	/** All members are non-zero. */
	FULL_DATE,
	/** Epoch is non-zero, the rest members are zero. */
	EPOCH_ONLY,
	/** 50/50 one of the above. */
	MIXED_LOAD,
};

uint32_t
mp_sizeof_xint(int64_t num)
{
	if (num >= 0)
		return mp_sizeof_uint(num);
	else
		return mp_sizeof_int(num);
}

char *
mp_encode_xint(char *data, int64_t num)
{
	if (num >= 0)
		return mp_encode_uint(data, num);
	else
		return mp_encode_int(data, num);
}

int64_t
mp_decode_xint(const char **data)
{
	if (mp_typeof(**data) == MP_UINT)
		return mp_decode_uint(data);
	else
		return mp_decode_int(data);
}

template <class DATETIME, encode_t ENCODE>
void decode(DATETIME& dt, const char *&data)
{
	int8_t type;
	uint32_t s = mp_decode_extl(&data, &type);
	if (type != EXT_TYPE)
		abort();
	const char* data_end = data + s;

	if (ENCODE == FMT_MP_FULL) {
		int64_t epoch = mp_decode_xint(&data);
		dt.epoch = epoch;
		dt.tzoffset = mp_decode_uint(&data);
		dt.tzindex = mp_decode_uint(&data);
		dt.nsec = mp_decode_uint(&data);
	} else if (ENCODE == FMT_MP_PARTIAL) {
		memset(&dt, 0, sizeof(dt));
		if (data == data_end)
			return;

		int64_t epoch = mp_decode_xint(&data);
		dt.epoch = epoch;

		if (data < data_end) {
			dt.tzoffset = mp_decode_uint(&data);
			if (data < data_end) {
				dt.tzindex = mp_decode_uint(&data);
				if (data < data_end)
					dt.nsec = mp_decode_uint(&data);
			}
		}
	} else if (ENCODE == FMT_RAW_FULL) {
		memcpy(&dt, data, sizeof(dt));
		data += sizeof(dt);
	} else if (ENCODE == FMT_RAW_PARTIAL) {
		if (s == 8) {
			memset(&dt, 0, sizeof(dt));
			memcpy(&dt.epoch, data, sizeof(dt.epoch));
			data += sizeof(dt.epoch);
		} else {
			memcpy(&dt, data, sizeof(dt));
			data += sizeof(dt);
		}
	} else {
		mp_unreachable();
	}

	if (data != data_end)
		abort();
}

template <class DATETIME, encode_t ENCODE>
void encode(const DATETIME& dt, char *&data)
{
	const char *was = data;
	if (ENCODE == FMT_MP_FULL) {
		int64_t epoch = (int64_t)dt.epoch;
		size_t s = 0;
		s += mp_sizeof_xint(epoch);
		s += mp_sizeof_uint(dt.tzoffset);
		s += mp_sizeof_uint(dt.tzindex);
		s += mp_sizeof_uint(dt.nsec);
		data = mp_encode_extl(data, EXT_TYPE, s);
		data = mp_encode_xint(data, epoch);
		data = mp_encode_uint(data, dt.tzoffset);
		data = mp_encode_uint(data, dt.tzindex);
		data = mp_encode_uint(data, dt.nsec);
	} else if (ENCODE == FMT_MP_PARTIAL) {
		int64_t epoch = (int64_t)dt.epoch;
		size_t s = 0;
		if (dt.nsec != 0) {
			s += mp_sizeof_xint(epoch);
			s += mp_sizeof_uint(dt.tzoffset);
			s += mp_sizeof_uint(dt.tzindex);
			s += mp_sizeof_uint(dt.nsec);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
			data = mp_encode_uint(data, dt.tzoffset);
			data = mp_encode_uint(data, dt.tzindex);
			data = mp_encode_uint(data, dt.nsec);
		} else if (dt.tzindex != 0) {
			s += mp_sizeof_xint(epoch);
			s += mp_sizeof_uint(dt.tzoffset);
			s += mp_sizeof_uint(dt.tzindex);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
			data = mp_encode_uint(data, dt.tzoffset);
			data = mp_encode_uint(data, dt.tzindex);
		} else if (dt.tzoffset != 0) {
			s += mp_sizeof_xint(epoch);
			s += mp_sizeof_uint(dt.tzoffset);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
			data = mp_encode_uint(data, dt.tzoffset);
		} else if (epoch != 0) {
			s += mp_sizeof_xint(epoch);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
		} else {
			data = mp_encode_extl(data, EXT_TYPE, s);
		}
	} else if (ENCODE == FMT_RAW_FULL) {
		data = mp_encode_extl(data, EXT_TYPE, sizeof(dt));
		memcpy(data, &dt, sizeof(dt));
		data += sizeof(dt);
	} else if (ENCODE == FMT_RAW_PARTIAL) {
		if (dt.tzoffset == 0 && dt.tzindex == 0 && dt.nsec == 0) {
			data = mp_encode_extl(data, EXT_TYPE, sizeof(dt.epoch));
			memcpy(data, &dt.epoch, sizeof(dt.epoch));
			data += sizeof(dt.epoch);
		} else {
			data = mp_encode_extl(data, EXT_TYPE, sizeof(dt));
			memcpy(data, &dt, sizeof(dt));
			data += sizeof(dt);
		}
	} else {
		mp_unreachable();
	}
	DATETIME tmp;
	decode<DATETIME, ENCODE>(tmp, was);
	if (was != data)
		abort();
	if (cmp(dt, tmp) != 0)
		abort();
	tmp.epoch++;
	if (cmp(dt, tmp) != -1)
		abort();
}

template <class DATETIME, workload_t WORKLOAD>
DATETIME generate_one()
{
	DATETIME res;
	memset(&res, 0, sizeof(res));
	int64_t epoch = 1634286411 + rand() % DIFFERENT_TIMESTAMPS;
	res.epoch = epoch;
	if (WORKLOAD == EPOCH_ONLY)
		return res;
	if (WORKLOAD == MIXED_LOAD) {
		if (rand() % 2)
			return res;
	}
	res.nsec = rand();
	res.tzoffset = (rand() % (2 * TZOFFSET_RANGE)) - TZOFFSET_RANGE;
	res.tzindex = rand() % TZINDEX_RANGE;
	return res;
}

template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
void generate(size_t& generated_size, size_t& generated_count)
{
	char *p1 = data1;
	char *p2 = data2;
	DATETIME to_sort[NUM_TEST_DATES];
	for (size_t i = 0; i < NUM_TEST_DATES; i++)
		to_sort[i] = generate_one<DATETIME, WORKLOAD>();
	std::sort(to_sort, to_sort + NUM_TEST_DATES,
		  [](const DATETIME& a, const DATETIME& b) {
			  return std::tie(a.epoch, a.nsec, a.tzoffset, a.tzindex) <
				 std::tie(b.epoch, b.nsec, b.tzoffset, b.tzindex);
		  });
	for (size_t i = 0; i < NUM_TEST_DATES; i++) {
		data1_ptrs[i] = p1;
		encode<DATETIME, ENCODE>(to_sort[i], p1);
		data2_ptrs[i] = p2;
		encode<DATETIME, ENCODE>(generate_one<DATETIME, WORKLOAD>(), p2);
	}

	generated_size += (p1 - data1) + (p2 - data2);
	generated_count += 2 * NUM_TEST_DATES;
}

template <class DATETIME, encode_t ENCODE>
size_t binary_search(const char *p, size_t& cmp_count)
{
	size_t b = 0;
	size_t e = NUM_TEST_DATES;
	while (e - b > 1) {
		size_t m = b + (e - b) / 2;
		DATETIME dt1, dt2;
		const char* p1 = p;
		decode<DATETIME, ENCODE>(dt1, p1);
		const char* p2 = data1_ptrs[m];
		decode<DATETIME, ENCODE>(dt2, p2);
		int c = cmp(dt1, dt2);
		cmp_count++;
		if (c == 0)
			return m;
		if (c < 0)
			e = m;
		else
			b = m + 1;
	}
	return b;
}

template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
static void
bench(benchmark::State& state)
{
	size_t compare_count = 0;
	size_t gen_size = 0, gen_count = 0;

	size_t i = NUM_TEST_DATES;

	for (auto _ : state) {
		if (i == NUM_TEST_DATES) {
			state.PauseTiming();
			i = 0;
			generate<DATETIME, ENCODE, WORKLOAD>(gen_size, gen_count);
			state.ResumeTiming();
		}
		benchmark::DoNotOptimize(
			binary_search<DATETIME, ENCODE>(data2_ptrs[i],
							compare_count));
		i++;
	}
	state.SetItemsProcessed(compare_count);
	double avg_size = double(gen_size) / gen_count;
	avg_size = double(size_t(avg_size * 10 + 0.5)) / 10;
	state.counters["avg_size"] = avg_size;
}

BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_MP_PARTIAL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_RAW_PARTIAL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_MP_PARTIAL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_RAW_PARTIAL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_MP_PARTIAL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, dbl_epoch, FMT_RAW_PARTIAL, MIXED_LOAD);

BENCHMARK_TEMPLATE(bench, int_epoch, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_MP_PARTIAL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_RAW_PARTIAL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_MP_PARTIAL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_RAW_PARTIAL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_MP_PARTIAL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, int_epoch, FMT_RAW_PARTIAL, MIXED_LOAD);

BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_PARTIAL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_PARTIAL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_PARTIAL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_PARTIAL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_PARTIAL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_PARTIAL, MIXED_LOAD);

BENCHMARK_MAIN();

static void
show_warning_if_debug()
{
#ifndef NDEBUG
	std::cerr << "#######################################################\n"
		  << "#######################################################\n"
		  << "#######################################################\n"
		  << "###                                                 ###\n"
		  << "###                    WARNING!                     ###\n"
		  << "###   The performance test is run in debug build!   ###\n"
		  << "###   Test results are definitely inappropriate!    ###\n"
		  << "###                                                 ###\n"
		  << "#######################################################\n"
		  << "#######################################################\n"
		  << "#######################################################\n";
#endif // #ifndef NDEBUG
}

struct DebugWarning {
	DebugWarning() { show_warning_if_debug(); }
} debug_warning;
