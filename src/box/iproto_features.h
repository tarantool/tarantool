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

/** IPROTO protocol feature ids returned by the IPROTO_ID command. */
#define IPROTO_FEATURES(_)						\
	/**
	 * Streams support: IPROTO_STREAM_ID header key.
	 */								\
	_(STREAMS, 0)							\
	/**
	 * Transactions in the protocol:
	 * IPROTO_BEGIN, IPROTO_COMMIT, IPROTO_ROLLBACK commands.
	 */								\
	_(TRANSACTIONS, 1)						\
	/**
	 * MP_ERROR MsgPack extension support.
	 *
	 * If a client doesn't set this feature bit, then errors returned by
	 * CALL/EVAL commands will be encoded according to the serialization
	 * rules for generic cdata/userdata Lua objects irrespective of the
	 * value of the msgpack.cfg.encode_errors_as_ext flag (by default
	 * converted to a string error message). If the feature bit is set and
	 * encode_errors_as_ext is true, errors will be encoded as MP_ERROR
	 * MsgPack extension.
	 */								\
	_(ERROR_EXTENSION, 2)						\
	/**
	 * Remote watchers support:
	 * IPROTO_WATCH, IPROTO_UNWATCH, IPROTO_EVENT commands.
	 */								\
	_(WATCHERS, 3)							\
	/**
	 * Pagination support:
	 * IPROTO_AFTER_POSITION, IPROTO_AFTER_TUPLE, IPROTO_FETCH_POSITION
	 * request fields and IPROTO_POSITION response field.
	 */								\
	_(PAGINATION, 4)						\
	/**
	 * Using space [index] names instead of identifiers support:
	 * IPROTO_SPACE_NAME and IPROTO_INDEX_NAME fields in IPROTO_SELECT,
	 * IPROTO_UPDATE and IPROTO_DELETE request body;
	 * IPROTO_SPACE_NAME field in IPROTO_INSERT, IPROTO_REPLACE,
	 * IPROTO_UPDATE and IPROTO_UPSERT request body.
	 */								\
	_(SPACE_AND_INDEX_NAMES,  5)					\
	/** IPROTO_WATCH_ONCE request support. */			\
	_(WATCH_ONCE,  6)						\
	/**
	 * Tuple format in DML request responses support:
	 * Tuples in IPROTO_DATA response field are encoded as MP_TUPLE and
	 * tuple format is sent in IPROTO_TUPLE_FORMATS field.
	 */								\
	_(DML_TUPLE_EXTENSION, 7)					\
	/**
	 * Tuple format in call and eval request responses support:
	 * Tuples in IPROTO_DATA response field are encoded as MP_TUPLE and
	 * tuple formats are sent in IPROTO_TUPLE_FORMATS field.
	 */								\
	_(CALL_RET_TUPLE_EXTENSION, 8)					\
	/**
	 * Tuple format in call and eval request arguments support:
	 * Tuples in IPROTO_TUPLE request field are encoded as MP_TUPLE and
	 * tuple formats are received in IPROTO_TUPLE_FORMATS field.
	 */								\
	_(CALL_ARG_TUPLE_EXTENSION, 9)					\

#define IPROTO_FEATURE_MEMBER(s, v) IPROTO_FEATURE_ ## s = v,

enum iproto_feature_id {
	IPROTO_FEATURES(IPROTO_FEATURE_MEMBER)
	iproto_feature_id_MAX
};

/** IPROTO feature name by id. */
extern const char *iproto_feature_id_strs[];

/**
 * IPROTO protocol feature bit map.
 */
struct iproto_features {
	char bits[BITMAP_SIZE(iproto_feature_id_MAX)];
};

/**
 * Current IPROTO protocol version returned by the IPROTO_ID command.
 * It should be incremented every time a new feature is added or removed.
 * `box.iproto.protocol_version` needs to be updated correspondingly.
 */
enum {
	IPROTO_CURRENT_VERSION = 7,
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
 * Clears a bit in a IPROTO protocol feature bit map.
 */
static inline void
iproto_features_clear(struct iproto_features *features, int id)
{
	assert(id >= 0 && id < iproto_feature_id_MAX);
	bit_clear(features->bits, id);
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
