/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "bit/bit.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * IPROTO protocol feature ids returned by the IPROTO_ID command.
 */
enum iproto_feature_id {
	/**
	 * Streams support: IPROTO_STREAM_ID header key.
	 */
	IPROTO_FEATURE_STREAMS = 0,
	/**
	 * Transactions in the protocol:
	 * IPROTO_BEGIN, IPROTO_COMMIT, IPROTO_ROLLBACK commands.
	 */
	IPROTO_FEATURE_TRANSACTIONS = 1,
	iproto_feature_id_MAX,
};

/**
 * IPROTO protocol feature bit map.
 */
struct iproto_features {
	char bits[BITMAP_SIZE(iproto_feature_id_MAX)];
};

/**
 * Current IPROTO protocol version returned by the IPROTO_ID command.
 * It should be incremented every time a new feature is added or removed.
 */
enum {
	IPROTO_CURRENT_VERSION = 1,
};

/**
 * Current IPROTO protocol features returned by the IPROTO_ID command.
 */
extern struct iproto_features IPROTO_CURRENT_FEATURES;

/**
 * Initializes a IPROTO protocol feature bit map with all zeros.
 */
static inline void
iproto_features_create(struct iproto_features *features)
{
	memset(features, 0, sizeof(*features));
}

/**
 * Sets a bit in a IPROTO protocol feature bit map.
 */
static inline void
iproto_features_set(struct iproto_features *features, int id)
{
	assert(id >= 0 && id < iproto_feature_id_MAX);
	bit_set(features->bits, id);
}

/**
 * Returns true if a feature is set in a IPROTO protocol feature bit map.
 */
static inline bool
iproto_features_test(const struct iproto_features *features, int id)
{
	assert(id >= 0 && id < iproto_feature_id_MAX);
	return bit_test(features->bits, id);
}

/**
 * Iterates over all feature ids set in a IPROTO protocol featreus bit map.
 */
#define iproto_features_foreach(features, id) \
	for (int id = 0; id < iproto_feature_id_MAX; id++) \
		if (iproto_features_test((features), id))

/**
 * Returns the size of a IPROTO protocol feature bit map encoded in msgpack.
 */
uint32_t
mp_sizeof_iproto_features(const struct iproto_features *features);

/**
 * Encodes a IPROTO protocol feature bit map in msgpack.
 * Returns a pointer to the byte following the end of the encoded data.
 */
char *
mp_encode_iproto_features(char *data, const struct iproto_features *features);

/**
 * Decodes a IPROTO protocol features bit map from msgpack.
 * Advances the data pointer. Returns 0 on success, -1 on failure.
 */
int
mp_decode_iproto_features(const char **data, struct iproto_features *features);

/**
 * Initializes this module.
 */
void
iproto_features_init(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif
