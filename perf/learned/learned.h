/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2026, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "trivia/util.h"

/**
 * File formats shared by learned_gen and learned_bench.
 *
 * A key file has the SOSD layout: uint64 count followed by count sorted
 * unique uint64 keys, native byte order.
 *
 * An operation file starts with struct ops_header, then a bitmap of
 * key_count bits telling which keys of the key file are loaded into the
 * index before the operations start, then op_count struct op records.
 */

/** "LRNOPS01" in little endian. */
static const uint64_t OPS_MAGIC = 0x313053504f4e524cULL;

/** Operation types of an operation stream. */
enum op_type {
	/** Point lookup by the key. */
	OP_GET,
	/** The first element >= key. */
	OP_LOWER_BOUND,
	/** Lower bound of the key and aux - 1 next elements. */
	OP_SCAN,
	/** Number of elements in [key, aux). */
	OP_COUNT,
	/** Insert of a key which is not in the index. */
	OP_INSERT,
	/** Delete of a key which is in the index. */
	OP_DELETE,
	op_type_MAX,
};

/** Names of enum op_type members. */
static const char *const op_type_strs[] = {
	"get", "lower_bound", "scan", "count", "insert", "delete",
};

/** One operation of an operation stream. */
struct op {
	/** The key. */
	uint64_t key;
	/** Scan length for OP_SCAN, the exclusive upper key for OP_COUNT. */
	uint64_t aux;
	/** Member of enum op_type. */
	uint32_t type;
	/** Position of the key in the key file. */
	uint32_t idx;
};

/** Header of an operation file. */
struct ops_header {
	/** Always OPS_MAGIC. */
	uint64_t magic;
	/** Number of keys in the key file the stream was made for. */
	uint64_t key_count;
	/** keys_hash() of that key file. */
	uint64_t key_hash;
	/** Number of keys loaded into the index before the operations. */
	uint64_t init_count;
	/** Number of operations. */
	uint64_t op_count;
};

/** Stand-in for a tuple, the indexes store pointers to them. */
struct rec {
	/** The indexed key. */
	uint64_t key;
	/** The rest of the tuple, makes the size realistic. */
	char data[24];
};

/** Adds a record to the hash of a scan, the order matters. */
static inline uint64_t
scan_hash(uint64_t hash, const struct rec *rec)
{
	return hash * 31 + (uintptr_t)rec;
}

/** Prints the message to stderr and exits with code 1. */
NORETURN static inline void
die(const char *fmt, ...) CFORMAT(printf, 1, 2);

NORETURN static inline void
die(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(1);
}

static inline FILE *
xfopen(const char *path, const char *mode)
{
	FILE *f = fopen(path, mode);
	if (f == NULL)
		die("cannot open %s: %s", path, strerror(errno));
	return f;
}

static inline void
xfread(void *buf, size_t size, FILE *f, const char *path)
{
	if (size > 0 && fread(buf, size, 1, f) != 1)
		die("cannot read %s: truncated file", path);
}

static inline void
xfwrite(const void *buf, size_t size, FILE *f, const char *path)
{
	if (size > 0 && fwrite(buf, size, 1, f) != 1)
		die("cannot write %s: %s", path, strerror(errno));
}

/** Size of an open file, leaves the position at its beginning. */
static inline uint64_t
xfsize(FILE *f, const char *path)
{
	if (fseeko(f, 0, SEEK_END) != 0)
		die("cannot seek %s: %s", path, strerror(errno));
	off_t size = ftello(f);
	if (size < 0 || fseeko(f, 0, SEEK_SET) != 0)
		die("cannot seek %s: %s", path, strerror(errno));
	return size;
}

/**
 * Loads a key file, checks its size and that the keys are sorted and unique.
 */
static inline std::vector<uint64_t>
keys_load(const char *path)
{
	FILE *f = xfopen(path, "rb");
	uint64_t file_size = xfsize(f, path);
	uint64_t count;
	xfread(&count, sizeof(count), f, path);
	if (file_size % sizeof(uint64_t) != 0 ||
	    file_size / sizeof(uint64_t) - 1 != count)
		die("%s: not a key file or it is truncated", path);
	std::vector<uint64_t> keys(count);
	xfread(keys.data(), count * sizeof(uint64_t), f, path);
	fclose(f);
	for (size_t i = 1; i < keys.size(); i++) {
		if (keys[i - 1] >= keys[i])
			die("%s: keys are not sorted and unique at %zu",
			    path, i);
	}
	return keys;
}

static inline void
keys_save(const char *path, const std::vector<uint64_t> &keys)
{
	FILE *f = xfopen(path, "wb");
	uint64_t count = keys.size();
	xfwrite(&count, sizeof(count), f, path);
	xfwrite(keys.data(), count * sizeof(uint64_t), f, path);
	fclose(f);
}

/** Order-dependent hash of a key array to pair op files with key files. */
static inline uint64_t
keys_hash(const std::vector<uint64_t> &keys)
{
	uint64_t h = 0xcbf29ce484222325ULL;
	for (uint64_t k : keys) {
		h ^= k;
		h *= 0x100000001b3ULL;
		h ^= h >> 29;
	}
	return h;
}

/** Number of 64-bit words in a bitmap of the given number of bits. */
static inline size_t
bitmap_words(size_t bits)
{
	return (bits + 63) / 64;
}

static inline bool
bitmap_test(const std::vector<uint64_t> &bitmap, size_t i)
{
	return (bitmap[i / 64] >> (i % 64)) & 1;
}

static inline void
bitmap_set(std::vector<uint64_t> &bitmap, size_t i)
{
	bitmap[i / 64] |= 1ULL << (i % 64);
}
