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
#include <cmath>
#include <iostream>
#include <tuple>

#include <msgpuck.h>

#include <benchmark/benchmark.h>

#include "dt.h"
#include "lineitem.h"
#include "trivia/util.h"

/**
 * The benchmark is based on binary search in a sorted array.
 */

/**
 * Number of dates in which a binary search will be executed.
 * For symmetry is the same number of dates that are searched in binary search.
 */
const size_t NUM_TEST_DATES = 8 * 1024;
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
/**
 * Days offset of Unix Epoch (1970-01-01) since Rata Die day (0001-01-01)
 */
const size_t DAYS_EPOCH_OFFSET = 719163;
/**
 * Number of seconds in day
 */
const size_t SECS_PER_DAY = 86400;
/**
 * Offset of "Tarantool Epoch" - 2011-01-01 since Unix Epoch
 */
const int64_t TARANTOOL_EPOCH_SHIFT = 1293840000;

/** The second dimension of the bench - msgpack serialization format. */
enum encode_t {
	/** All members are msgpack encoded in MP_EXT data. */
	FMT_MP_FULL,
	/** Some (basically nonzero) members are mp encoded in MP_EXT data. */
	FMT_MP_NONZERO,
	/** All the structure is directly copied to MP_EXT data. */
	FMT_RAW_FULL,
	/** Conditionally nonzero part of structure is copied to MP_EXT data. */
	FMT_RAW_NONZERO,
	/** Shift epoch closer to Tarantool epoch */
	FMT_TNT_EPOCH,
	/** Save separately date and seconds parts */
	FMT_MP_DATE,
	/** Save date separately, with shift to Tarantool epoch */
	FMT_TNT_EPOCH_DATE,
};

/** The third dimension of the bench - datetime variety workload. */
enum workload_t {
	/** All members are non-zero. */
	FULL_DATE,
	/** Epoch is non-zero, the rest members are zero. */
	EPOCH_ONLY,
	/** 50/50 one of the above. */
	MIXED_LOAD,
	/** TPCH generated data */
	TPCH_1COLUMN,
	TPCH_ALLCOLUMNS,
};

/** Static data holder. */
template <class DATETIME, workload_t WORKLOAD>
static char data1[32 * NUM_TEST_DATES];

template <class DATETIME, workload_t WORKLOAD>
static char data2[32 * NUM_TEST_DATES];

template <class DATETIME, workload_t WORKLOAD>
static const char *data1_ptrs[NUM_TEST_DATES];

template <class DATETIME, workload_t WORKLOAD>
static const char *data2_ptrs[NUM_TEST_DATES];

template <class DATETIME, workload_t WORKLOAD>
static DATETIME input_data[NUM_TEST_DATES];

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
		return cmp_args(a.epoch, b.epoch, a.nsec, b.nsec);
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
		return cmp_args(a.epoch, b.epoch, a.nsec, b.nsec);
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
	int64_t shift = 0;
	int64_t epoch;
	int64_t date;
	const size_t sz_tail = sizeof(dt) - sizeof(int64_t);

	switch (ENCODE) {
	case FMT_MP_FULL:
		epoch = mp_decode_xint(&data);
		dt.epoch = epoch;
		dt.tzoffset = mp_decode_uint(&data);
		dt.tzindex = mp_decode_uint(&data);
		dt.nsec = mp_decode_uint(&data);
		break;

	case FMT_TNT_EPOCH:
		shift = TARANTOOL_EPOCH_SHIFT;
		/* fallthru */
	case FMT_MP_NONZERO:
		memset(&dt, 0, sizeof(dt));
		if (data == data_end)
			return;

		epoch = mp_decode_xint(&data);
		dt.epoch = epoch + shift;

		if (likely(data < data_end)) {
			dt.tzoffset = mp_decode_uint(&data);
			if (data < data_end) {
				dt.tzindex = mp_decode_uint(&data);
				if (data < data_end)
					dt.nsec = mp_decode_uint(&data);
			}
		}
		break;

	case FMT_TNT_EPOCH_DATE:
		shift = TARANTOOL_EPOCH_SHIFT;
		/* fallthru */
	case FMT_MP_DATE:
		memset(&dt, 0, sizeof(dt));
		if (data == data_end)
			return;

		date = mp_decode_xint(&data);
		dt.epoch = date * SECS_PER_DAY + shift;

		if (likely(data < data_end)) {
			int secs = mp_decode_xint(&data);
			dt.epoch += secs;
			if (data < data_end) {
				dt.tzoffset = mp_decode_uint(&data);
				if (data < data_end) {
					dt.tzindex = mp_decode_uint(&data);
					if (data < data_end)
						dt.nsec = mp_decode_uint(&data);
				}
			}
		}
		break;

	case FMT_RAW_FULL:
		dt.epoch = *(int64_t *)data;
		data += sizeof(int64_t);

		memcpy(&dt.nsec, data, sz_tail);
		data += sz_tail;
		break;

	case FMT_RAW_NONZERO:
		dt.epoch = *(int64_t *)data;
		data += sizeof(int64_t);
		if (unlikely(s > sizeof(int64_t))) {
			memcpy(&dt.nsec, data, sz_tail);
			data += sz_tail;
		}
		break;

	default:
		mp_unreachable();
	}

#ifndef NDEBUG
	if (data != data_end)
		abort();
#endif
}

template <class DATETIME, encode_t ENCODE>
void encode(const DATETIME& dt, char *&data)
{
	const char *was = data;
	int64_t epoch;
	int64_t shift = 0;
	size_t s = 0;
	int64_t date, secs;
	const size_t sz_tail = sizeof(dt) - sizeof(int64_t);

	switch (ENCODE) {
	case FMT_MP_FULL:
		epoch = (int64_t)dt.epoch;
		s = 0;
		s += mp_sizeof_xint(epoch);
		s += mp_sizeof_uint(dt.tzoffset);
		s += mp_sizeof_uint(dt.tzindex);
		s += mp_sizeof_uint(dt.nsec);
		data = mp_encode_extl(data, EXT_TYPE, s);
		data = mp_encode_xint(data, epoch);
		data = mp_encode_uint(data, dt.tzoffset);
		data = mp_encode_uint(data, dt.tzindex);
		data = mp_encode_uint(data, dt.nsec);
		break;

	case FMT_TNT_EPOCH:
		shift = TARANTOOL_EPOCH_SHIFT;
		/* fallthru */
	case FMT_MP_NONZERO:
		epoch = (int64_t)dt.epoch - shift;
		s = 0;
		if (unlikely(dt.nsec != 0)) {
			s += mp_sizeof_xint(epoch);
			s += mp_sizeof_uint(dt.tzoffset);
			s += mp_sizeof_uint(dt.tzindex);
			s += mp_sizeof_uint(dt.nsec);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
			data = mp_encode_uint(data, dt.tzoffset);
			data = mp_encode_uint(data, dt.tzindex);
			data = mp_encode_uint(data, dt.nsec);
		} else if (unlikely(dt.tzindex != 0)) {
			s += mp_sizeof_xint(epoch);
			s += mp_sizeof_uint(dt.tzoffset);
			s += mp_sizeof_uint(dt.tzindex);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
			data = mp_encode_uint(data, dt.tzoffset);
			data = mp_encode_uint(data, dt.tzindex);
		} else if (unlikely(dt.tzoffset != 0)) {
			s += mp_sizeof_xint(epoch);
			s += mp_sizeof_uint(dt.tzoffset);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
			data = mp_encode_uint(data, dt.tzoffset);
		} else if (likely(epoch != 0)) {
			s += mp_sizeof_xint(epoch);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, epoch);
		} else {
			data = mp_encode_extl(data, EXT_TYPE, s);
		}
		break;

	case FMT_TNT_EPOCH_DATE:
		shift = TARANTOOL_EPOCH_SHIFT;
		/* fallthru */
	case FMT_MP_DATE:
		epoch = (int64_t)dt.epoch - shift;
		s = 0;
		date = epoch / SECS_PER_DAY;
		secs = epoch % SECS_PER_DAY;
		if (unlikely(dt.nsec != 0)) {
			s += mp_sizeof_xint(date);
			s += mp_sizeof_xint(secs);
			s += mp_sizeof_uint(dt.tzoffset);
			s += mp_sizeof_uint(dt.tzindex);
			s += mp_sizeof_uint(dt.nsec);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, date);
			data = mp_encode_xint(data, secs);
			data = mp_encode_uint(data, dt.tzoffset);
			data = mp_encode_uint(data, dt.tzindex);
			data = mp_encode_uint(data, dt.nsec);
		} else if (unlikely(dt.tzindex != 0)) {
			s += mp_sizeof_xint(date);
			s += mp_sizeof_xint(secs);
			s += mp_sizeof_uint(dt.tzoffset);
			s += mp_sizeof_uint(dt.tzindex);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, date);
			data = mp_encode_xint(data, secs);
			data = mp_encode_uint(data, dt.tzoffset);
			data = mp_encode_uint(data, dt.tzindex);
		} else if (unlikely(dt.tzoffset != 0)) {
			s += mp_sizeof_xint(date);
			s += mp_sizeof_xint(secs);
			s += mp_sizeof_uint(dt.tzoffset);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, date);
			data = mp_encode_xint(data, secs);
			data = mp_encode_uint(data, dt.tzoffset);
		} else if (unlikely(secs != 0)) {
			s += mp_sizeof_xint(date);
			s += mp_sizeof_xint(secs);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, date);
			data = mp_encode_xint(data, secs);
		} else if (likely(epoch != 0)) {
			s += mp_sizeof_xint(date);
			data = mp_encode_extl(data, EXT_TYPE, s);
			data = mp_encode_xint(data, date);
		} else {
			data = mp_encode_extl(data, EXT_TYPE, s);
		}
		break;

	case FMT_RAW_FULL:
		data = mp_encode_extl(data, EXT_TYPE, sizeof(dt));
		*(int64_t *)data = (int64_t)dt.epoch;
		data += sizeof(int64_t);
		memcpy(data, &dt.nsec, sz_tail);
		data += sz_tail;
		break;

	case FMT_RAW_NONZERO:
		if (likely(dt.tzoffset == 0 && dt.tzindex == 0 &&
			   dt.nsec == 0)) {
			data = mp_encode_extl(data, EXT_TYPE, sizeof(int64_t));
			*(int64_t *)data = (int64_t)dt.epoch;
			data += sizeof(int64_t);
		} else {
			data = mp_encode_extl(data, EXT_TYPE, sizeof(dt));
			*(int64_t *)data = (int64_t)dt.epoch;
			data += sizeof(int64_t);
			memcpy(data, &dt.nsec, sz_tail);
			data += sz_tail;
		}
		break;

	default:
		mp_unreachable();
	}
#ifndef NDEBUG
	DATETIME tmp;
	decode<DATETIME, ENCODE>(tmp, was);
	if (was != data)
		abort();
	if (cmp(dt, tmp) != 0)
		abort();
	tmp.epoch++;
	if (cmp(dt, tmp) != -1)
		abort();
#endif
}

template <class DATETIME, workload_t WORKLOAD>
DATETIME
generate_one(int i)
{
	if (WORKLOAD == TPCH_1COLUMN) {
		const struct lineitem &item = lineitem[i];
		const struct datetime &date = item.l_receiptdate;

		DATETIME res = {};
		dt_t dt = dt_from_ymd(date.year, date.month, date.day);
		res.epoch = (dt - DAYS_EPOCH_OFFSET) * SECS_PER_DAY;

		return res;
	} else {
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
}

template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
void generate(benchmark::State &state, size_t &generated_size,
	      size_t &generated_count)
{
	auto p1 = data1<DATETIME, WORKLOAD>;

	for (size_t i = 0; i < NUM_TEST_DATES; i++) {
		data1_ptrs<DATETIME, WORKLOAD>[i] = p1;
		encode<DATETIME, ENCODE>(input_data<DATETIME, WORKLOAD>[i], p1);
	}

	generated_size += p1 - data1<DATETIME, WORKLOAD>;
	generated_count += NUM_TEST_DATES;
}

// should be outside of benchmark loop
template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
void
sort(benchmark::State &state)
{
	char *p1 = data1<DATETIME, WORKLOAD>;
	char *p2 = data2<DATETIME, WORKLOAD>;
	DATETIME to_sort[NUM_TEST_DATES];

	for (size_t i = 0; i < NUM_TEST_DATES; i++)
		to_sort[i] = input_data<DATETIME, WORKLOAD>[i];

	std::sort(to_sort, to_sort + NUM_TEST_DATES,
		  [](const DATETIME &a, const DATETIME &b) {
			  return std::tie(a.epoch, a.nsec) <
				  std::tie(b.epoch, b.nsec);
		  });

	for (size_t i = 0; i < NUM_TEST_DATES; i++) {
		data1_ptrs<DATETIME, WORKLOAD>[i] = p1;
		encode<DATETIME, ENCODE>(to_sort[i], p1);
	}
}

template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
size_t
binary_search(const char *p, size_t &cmp_count)
{
	size_t b = 0;
	size_t e = NUM_TEST_DATES;
	while (e - b > 1) {
		size_t m = b + (e - b) / 2;
		DATETIME dt1, dt2;
		const char* p1 = p;
		decode<DATETIME, ENCODE>(dt1, p1);
		const char *p2 = data1_ptrs<DATETIME, WORKLOAD>[m];
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

template <class DATETIME, workload_t WORKLOAD>
void
setup()
{
	for (size_t i = 0; i < NUM_TEST_DATES; i++)
		input_data<DATETIME, WORKLOAD>[i] =
			generate_one<DATETIME, WORKLOAD>(i);
};

template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
static void
bench_encode(benchmark::State &state)
{
	size_t gen_size = 0, gen_count = 0;
	size_t i = NUM_TEST_DATES;

	for (auto _ : state) {
		if (i == NUM_TEST_DATES) {
			i = 0;
			generate<DATETIME, ENCODE, WORKLOAD>(state, gen_size,
							     gen_count);
		}
		i++;
	}
	double avg_size = double(gen_size) / gen_count;
	avg_size = std::round(avg_size * 10.0) / 10.0;
	state.counters["avg_size"] = avg_size;
}

template <class DATETIME, encode_t ENCODE, workload_t WORKLOAD>
static void
bench_decode_search(benchmark::State &state)
{
	sort<DATETIME, ENCODE, WORKLOAD>(state);

	size_t compare_count = 0;
	size_t i = NUM_TEST_DATES;

	for (auto _ : state) {
		if (i == NUM_TEST_DATES)
			i = 0;

		benchmark::DoNotOptimize(
			binary_search<DATETIME, ENCODE, WORKLOAD>(
				data1_ptrs<DATETIME, WORKLOAD>[i],
				compare_count));
		i++;
	}
	state.SetItemsProcessed(compare_count);
}

BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH_DATE, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH_DATE,
		   FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_DATE, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_DATE, FULL_DATE);

BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH_DATE, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH_DATE,
		   EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_DATE, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_DATE, EPOCH_ONLY);

BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH_DATE, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH_DATE,
		   MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_DATE, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_DATE, MIXED_LOAD);

BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_FULL, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_FULL, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_NONZERO, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_NONZERO,
		   TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_TNT_EPOCH_DATE, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_TNT_EPOCH_DATE,
		   TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_FULL, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_FULL, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_RAW_NONZERO, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_RAW_NONZERO,
		   TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_encode, dbl_epoch, FMT_MP_DATE, TPCH_1COLUMN);
BENCHMARK_TEMPLATE(bench_decode_search, dbl_epoch, FMT_MP_DATE, TPCH_1COLUMN);

BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_TNT_EPOCH, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_TNT_EPOCH, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_TNT_EPOCH_DATE, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_TNT_EPOCH_DATE,
		   FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_RAW_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_RAW_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_DATE, FULL_DATE);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_DATE, FULL_DATE);

BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_TNT_EPOCH, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_TNT_EPOCH, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_TNT_EPOCH_DATE, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_TNT_EPOCH_DATE,
		   EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_RAW_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_RAW_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_DATE, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_DATE, EPOCH_ONLY);

BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_TNT_EPOCH, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_TNT_EPOCH, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_TNT_EPOCH_DATE, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_TNT_EPOCH_DATE,
		   MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_RAW_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_RAW_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_encode, int_epoch, FMT_MP_DATE, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench_decode_search, int_epoch, FMT_MP_DATE, MIXED_LOAD);

#if 0
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_FULL, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_NONZERO, FULL_DATE);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_FULL, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_NONZERO, EPOCH_ONLY);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, reordered, FMT_MP_NONZERO, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_FULL, MIXED_LOAD);
BENCHMARK_TEMPLATE(bench, reordered, FMT_RAW_NONZERO, MIXED_LOAD);
#endif

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

struct Singleton {
	Singleton()
	{
		show_warning_if_debug();
		std::cout << "setting up benchmark data" << std::endl;
		srand(20110101);
		setup<dbl_epoch, FULL_DATE>();
		setup<dbl_epoch, EPOCH_ONLY>();
		setup<dbl_epoch, MIXED_LOAD>();
		setup<dbl_epoch, TPCH_1COLUMN>();

		setup<int_epoch, FULL_DATE>();
		setup<int_epoch, EPOCH_ONLY>();
		setup<int_epoch, MIXED_LOAD>();
		setup<int_epoch, TPCH_1COLUMN>();
	}
} singleton;
