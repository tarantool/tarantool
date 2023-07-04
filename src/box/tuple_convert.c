/*
 * Copyright 2010-2016, Tarantool AUTHORS, please see AUTHORS file.
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
#include "tuple.h"
#include <msgpuck/msgpuck.h>
#include <yaml.h>
#include <base64.h>
#include <small/region.h>
#include <small/obuf.h>
#include "fiber.h"
#include <trivia/util.h>
#include <box/mp_tuple.h>

int
tuple_to_obuf(struct tuple *tuple, struct obuf *buf)
{
	uint32_t bsize;
	const char *data = tuple_data_range(tuple, &bsize);
	if (obuf_dup(buf, data, bsize) != bsize) {
		diag_set(OutOfMemory, bsize, "tuple_to_obuf", "dup");
		return -1;
	}
	return 0;
}

int
tuple_to_obuf_as_ext(struct tuple *tuple, struct obuf *buf)
{
	uint32_t tuple_sz = mp_sizeof_tuple(tuple);
	char *data = obuf_alloc(buf, tuple_sz);
	if (data == NULL) {
		diag_set(OutOfMemory, tuple_sz, "obuf_alloc", "buf");
		return -1;
	}
	mp_encode_tuple(data, tuple);
	return 0;
}

int
append_output(void *arg, unsigned char *buf, size_t len)
{
	(void) arg;
	char *buf_out = region_alloc(&fiber()->gc, len + 1);
	if (!buf_out) {
		diag_set(OutOfMemory, len , "region", "tuple_to_yaml");
		return 0;
	}
	memcpy(buf_out, buf, len);
	buf_out[len] = '\0';
	return 1;
}

static int
encode_node(yaml_emitter_t *emitter, const char **data);

static int
encode_table(yaml_emitter_t *emitter, const char **data)
{
	yaml_event_t ev;
	yaml_mapping_style_t yaml_style = YAML_FLOW_MAPPING_STYLE;
	if (!yaml_mapping_start_event_initialize(&ev, NULL, NULL, 0, yaml_style)
			|| !yaml_emitter_emit(emitter, &ev)) {
		diag_set(SystemError, "failed to init event libyaml");
		return 0;
	}

	uint32_t size = mp_decode_map(data);
	for (uint32_t i = 0; i < size; i++) {
		if (!encode_node(emitter, data))
			return 0;
		if (!encode_node(emitter, data))
			return 0;
	}

	if (!yaml_mapping_end_event_initialize(&ev) ||
	    !yaml_emitter_emit(emitter, &ev)) {
		diag_set(SystemError, "failed to end event libyaml");
		return 0;
	}

	return 1;
}


static int
encode_array(yaml_emitter_t *emitter, const char **data)
{
	yaml_event_t ev;
	yaml_sequence_style_t yaml_style = YAML_FLOW_SEQUENCE_STYLE;
	if (!yaml_sequence_start_event_initialize(&ev, NULL, NULL, 0,
				yaml_style) ||
			!yaml_emitter_emit(emitter, &ev)) {
		diag_set(SystemError, "failed to init event libyaml");
		return 0;
	}

	uint32_t size = mp_decode_array(data);
	for (uint32_t i = 0; i < size; i++) {
		if (!encode_node(emitter, data))
		   return 0;
	}

	if (!yaml_sequence_end_event_initialize(&ev) ||
	    !yaml_emitter_emit(emitter, &ev)) {
		diag_set(SystemError, "failed to end event libyaml");
		return 0;
	}

	return 1;
}

#define LUAYAML_TAG_PREFIX "tag:yaml.org,2002:"

static int
encode_node(yaml_emitter_t *emitter, const char **data)
{
	size_t len = 0;
	const char *str = "";
	size_t binlen = 0;
	char *bin = NULL;
	yaml_char_t *tag = NULL;
	yaml_event_t ev;
	yaml_scalar_style_t style = YAML_PLAIN_SCALAR_STYLE;
	char buf[FPCONV_G_FMT_BUFSIZE];
	int type = mp_typeof(**data);
	switch(type) {
	case MP_UINT:
		len = snprintf(buf, sizeof(buf), "%llu",
			       (unsigned long long) mp_decode_uint(data));
		buf[len] = 0;
		str = buf;
		break;
	case MP_INT:
		len = snprintf(buf, sizeof(buf), "%lld",
			       (long long) mp_decode_int(data));
		buf[len] = 0;
		str = buf;
		break;
	case MP_FLOAT:
		fpconv_g_fmt(buf, mp_decode_float(data),
			     FPCONV_G_FMT_MAX_PRECISION);
		str = buf;
		len = strlen(buf);
		break;
	case MP_DOUBLE:
		fpconv_g_fmt(buf, mp_decode_double(data),
			     FPCONV_G_FMT_MAX_PRECISION);
		str = buf;
		len = strlen(buf);
		break;
	case MP_ARRAY:
		return encode_array(emitter, data);
	case MP_MAP:
		return encode_table(emitter, data);
	case MP_STR:
		len = mp_decode_strl(data);
		str = *data;
		*data += len;
		style = YAML_SINGLE_QUOTED_SCALAR_STYLE;
		break;
	case MP_BIN:
		len = mp_decode_binl(data);
		str = *data;
		*data += len;
		style = YAML_ANY_SCALAR_STYLE;
		binlen = base64_encode_bufsize(len, BASE64_NOWRAP);
		bin = (char *) malloc(binlen);
		if (bin == NULL) {
			diag_set(OutOfMemory, binlen, "malloc",
				 "tuple_to_yaml");
			return 0;
		}
		binlen = base64_encode(str, len, bin, binlen, BASE64_NOWRAP);
		str = bin;
		len = binlen;
		tag = (yaml_char_t *) LUAYAML_TAG_PREFIX "binary";
		break;
	case MP_BOOL:
		if (mp_decode_bool(data)) {
			str = "true";
			len = 4;
		} else {
			str = "false";
			len = 5;
		}
		break;
	case MP_NIL:
	case MP_EXT:
		if (type == MP_NIL) {
			mp_decode_nil(data);
		} else {
			mp_next(data);
		}
		style = YAML_PLAIN_SCALAR_STYLE;
		str = "null";
		len = 4;
		break;
	default:
		unreachable();
	}

	int rc = 1;
	if (!yaml_scalar_event_initialize(&ev, NULL, tag, (unsigned char *)str,
					  len, bin == NULL, bin == NULL,
					  style) ||
	    !yaml_emitter_emit(emitter, &ev)) {
		diag_set(OutOfMemory, len, "malloc", "tuple_to_yaml");
		rc = 0;
	}
	if (bin != NULL)
		free(bin);

	return rc;
}

char *
tuple_to_yaml(struct tuple *tuple)
{
	const char *data = tuple_data(tuple);
	yaml_emitter_t emitter;
	yaml_event_t ev;

	size_t used = region_used(&fiber()->gc);

	if (!yaml_emitter_initialize(&emitter)) {
		diag_set(SystemError, "failed to init libyaml");
		return NULL;
	}
	yaml_emitter_set_unicode(&emitter, 1);
	yaml_emitter_set_indent(&emitter, 2);
	yaml_emitter_set_width(&emitter, INT_MAX);
	yaml_emitter_set_output(&emitter, &append_output, NULL);

	if (!yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING) ||
	    !yaml_emitter_emit(&emitter, &ev) ||
	    !yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 1) ||
	    !yaml_emitter_emit(&emitter, &ev)) {
		diag_set(SystemError, "failed to init event libyaml");
		goto error;
	}
	if (!encode_node(&emitter, &data))
		goto error;

	if (!yaml_document_end_event_initialize(&ev, 1) ||
	    !yaml_emitter_emit(&emitter, &ev) ||
	    !yaml_stream_end_event_initialize(&ev) ||
	    !yaml_emitter_emit(&emitter, &ev) ||
	    !yaml_emitter_flush(&emitter)) {
		diag_set(SystemError, "failed to end event libyaml");
		goto error;
	}

	yaml_emitter_delete(&emitter);

	size_t total_len = region_used(&fiber()->gc) - used;
	char *buf = (char *) region_join(&fiber()->gc, total_len);
	if (buf == NULL) {
		diag_set(OutOfMemory, total_len, "region", "tuple_to_yaml");
		return NULL;
	}
	/* Remove trailing "\n\0" added by libyaml */
	assert(total_len > 2);
	assert(buf[total_len - 1] == '\0' && buf[total_len - 2] == '\n');
	buf[total_len - 2] = '\0';
	return buf;
error:
	yaml_emitter_delete(&emitter);
	return NULL;
}
