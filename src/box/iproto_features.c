/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */
#include "iproto_features.h"

#include <stdint.h>

#include "msgpuck.h"

struct iproto_features IPROTO_CURRENT_FEATURES;

uint32_t
mp_sizeof_iproto_features(const struct iproto_features *features)
{
	int count = 0;
	uint32_t size = 0;
	iproto_features_foreach(features, feature_id) {
		size += mp_sizeof_uint(feature_id);
		count++;
	}
	size += mp_sizeof_array(count);
	return size;
}

char *
mp_encode_iproto_features(char *data, const struct iproto_features *features)
{
	int count = 0;
	iproto_features_foreach(features, feature_id)
		count++;
	data = mp_encode_array(data, count);
	iproto_features_foreach(features, feature_id)
		data = mp_encode_uint(data, feature_id);
	return data;
}

int
mp_decode_iproto_features(const char **data, struct iproto_features *features)
{
	if (mp_typeof(**data) != MP_ARRAY)
		return -1;
	uint32_t size = mp_decode_array(data);
	for (uint32_t i = 0; i < size; i++) {
		if (mp_typeof(**data) != MP_UINT)
			return -1;
		uint64_t feature_id = mp_decode_uint(data);
		/* Ignore unknown features for forward compatibility. */
		if (feature_id >= iproto_feature_id_MAX)
			continue;
		iproto_features_set(features, feature_id);
	}
	return 0;
}

const struct iproto_constant iproto_feature_id_constants[] = {
	IPROTO_FEATURES(IPROTO_CONSTANT_MEMBER)
};

const size_t iproto_feature_id_constants_size =
	lengthof(iproto_feature_id_constants);

void
iproto_features_init(void)
{
	iproto_features_create(&IPROTO_CURRENT_FEATURES);
	iproto_features_set(&IPROTO_CURRENT_FEATURES,
			    IPROTO_FEATURE_STREAMS);
	iproto_features_set(&IPROTO_CURRENT_FEATURES,
			    IPROTO_FEATURE_TRANSACTIONS);
	iproto_features_set(&IPROTO_CURRENT_FEATURES,
			    IPROTO_FEATURE_ERROR_EXTENSION);
	iproto_features_set(&IPROTO_CURRENT_FEATURES,
			    IPROTO_FEATURE_WATCHERS);
	iproto_features_set(&IPROTO_CURRENT_FEATURES,
			    IPROTO_FEATURE_PAGINATION);
	iproto_features_set(&IPROTO_CURRENT_FEATURES,
			    IPROTO_FEATURE_SPACE_AND_INDEX_NAMES);
}
