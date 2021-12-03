/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
 */

#include "msgpuck_compression.h"
#include "space.h"
#include "trivia/util.h"
#include "unit.h"
#include "tt_uuid.h"
#include "mp_uuid.h"
#include "random.h"
#include "memory.h"
#include "fiber.h"
#include <small/region.h>

#include <stdlib.h>

#define MP_TYPE_SIZE_MAX 100
#define SPACE_FIELD_COUNT_MAX 10
#define EXTRA_FIELD_COUNT_MAX 100

static struct space *
space_random_new(uint32_t field_count)
{
        struct space *space = xcalloc(1, sizeof(struct space));
        space->def = xcalloc(1, sizeof(struct space_def));
        space->def->fields = xcalloc(field_count, sizeof(struct field_def));
        space->def->field_count = field_count;
        for (uint32_t i = 0; i < field_count; i++) {
                space->def->fields[i].compression_type =
                        rand() % compression_type_MAX;
                space->def->fields[i].type = rand() % field_type_MAX;
        }
        return space;
}

static void
space_random_delete(struct space *space)
{
        free(space->def->fields);
        free(space->def);
        free(space);
        TRASH(space);
}

static size_t
msgpuck_field_size_max(enum mp_type type)
{
        switch (type) {
        case MP_NIL:
                return mp_sizeof_nil();
        case MP_UINT:
        case MP_INT:
                return 9;
        case MP_STR:
        case MP_BIN:
        case MP_ARRAY:
        case MP_MAP:
                return MP_TYPE_SIZE_MAX;
        case MP_BOOL:
                return 1;
        case MP_FLOAT:
                return 5;
        case MP_DOUBLE:
                return 9;
        case MP_EXT:
                return mp_sizeof_uuid();
        default:;
        }
       abort();
}

static char *
msgpuck_random_str_field(char *data)
{
        char field[MP_TYPE_SIZE_MAX - 5];
        for (uint32_t i = 0; i < MP_TYPE_SIZE_MAX - 5; i++)
                field[i] = rand() % 128;
        return mp_encode_str(data, field, MP_TYPE_SIZE_MAX - 5);
}

static char *
msgpuck_random_bin_field(char *data)
{
        char field[MP_TYPE_SIZE_MAX - 5];
        for (uint32_t i = 0; i < MP_TYPE_SIZE_MAX - 5; i++)
                field[i] = rand() % 255;
        return mp_encode_bin(data, field, MP_TYPE_SIZE_MAX - 5);
}

static char *
msgpuck_random_array_field(char *data)
{
        uint32_t total_field_count = (MP_TYPE_SIZE_MAX - 5) / 9;
        char *data_end = data;
        data_end = mp_encode_array(data_end, total_field_count);
        for (uint32_t i = 0; i < total_field_count; i++)
                data_end = mp_encode_uint(data_end, rand());
        return data_end;
}

static char *
msgpuck_random_map_field(char *data)
{
        uint32_t total_field_count = (MP_TYPE_SIZE_MAX - 5) / (2 * 9);
        char *data_end = data;
        data_end = mp_encode_map(data_end, total_field_count);
        for (uint32_t i = 0; i < total_field_count; i++) {
                data_end = mp_encode_uint(data_end, rand());
                data_end = mp_encode_uint(data_end, rand());
        }
        return data_end;
}

static char *
msgpuck_random_ext_field(char *data)
{
        struct tt_uuid uuid;
        tt_uuid_create(&uuid);
        return mp_encode_uuid(data, &uuid);
}

static char *
msgpuck_random_field(char *data, enum mp_type type)
{
        switch (type) {
        case MP_NIL:
                return mp_encode_nil(data);
        case MP_UINT:
                return mp_encode_uint(data, rand());
        case MP_INT:
                return mp_encode_int(data, rand());
        case MP_STR:
                return msgpuck_random_str_field(data);
        case MP_BIN:
                return msgpuck_random_bin_field(data);
        case MP_ARRAY:
                return msgpuck_random_array_field(data);
        case MP_MAP:
                return msgpuck_random_map_field(data);
        case MP_BOOL:
                return mp_encode_bool(data, rand() % 2);
        case MP_FLOAT:
                return mp_encode_float(data, rand() / 1.375);
        case MP_DOUBLE:
                return mp_encode_double(data, rand() / 1.375);
        case MP_EXT:
                return msgpuck_random_ext_field(data);
        default:;
        }
       abort();
}

static enum mp_type
first_compatible_mp_type(enum field_type type)
{
        for (enum mp_type i = MP_NIL; i < MP_EXT; i++) {
                if (field_mp_plain_type_is_compatible(type, i, false))
                        return i;
        }
        return MP_EXT;
}

static char *
msgpuck_random_new(struct space *space, uint32_t extra_field_count,
                   uint32_t *new_msgpuck_len)
{
        /*
         * All extra fields have MP_UINT type (they do not affect the test).
         * So total msgpuck size have at least 5 (maximum array header size) +
         * count of extra fields * maximum MP_UINT size.
         */
        size_t total_size = 5 + 9 * extra_field_count;
        uint32_t total_field_count =
                space->def->field_count + extra_field_count;
        for (uint32_t i = 0; i < space->def->field_count; i++) {
                enum mp_type mp_type =
                        first_compatible_mp_type(space->def->fields[i].type);
                total_size += msgpuck_field_size_max(mp_type);
        }
        char *msgpuck = xmalloc(total_size);
        char *msgpuck_end = msgpuck;
        msgpuck_end = mp_encode_array(msgpuck_end, total_field_count);
        for (uint32_t i = 0; i < space->def->field_count; i++) {
                enum mp_type mp_type =
                        first_compatible_mp_type(space->def->fields[i].type);
                msgpuck_end = msgpuck_random_field(msgpuck_end, mp_type);
        }
        for (uint32_t i = 0; i < extra_field_count; i++)
                msgpuck_end = mp_encode_uint(msgpuck_end, rand());
        *new_msgpuck_len = msgpuck_end - msgpuck;
        assert(*new_msgpuck_len <= total_size);
        return msgpuck;
}

static void
msgpuck_random_delete(char *msgpuck)
{
        free(msgpuck);
}

static int
check_random_msgpuck_compression_decompression(void)
{
        plan(SPACE_FIELD_COUNT_MAX * 3);
        for (uint32_t i = 0; i < SPACE_FIELD_COUNT_MAX; i++) {
                struct space *space = space_random_new(i);
                uint32_t new_msgpuck_len;
                char *msgpuck = msgpuck_random_new(space, EXTRA_FIELD_COUNT_MAX,
                                                   &new_msgpuck_len);
                size_t used = region_used(&fiber()->gc);
                char *new_data, *new_data_end;
                int rc = msgpuck_compress_fields(space, msgpuck,
                                                 msgpuck + new_msgpuck_len,
                                                 &new_data, &new_data_end);
                is(rc, 0, "msgpuck compression");
                char *dmsgpuck;
                rc = msgpuck_decompress_fields(space, new_data, new_data_end,
                                               &dmsgpuck, new_msgpuck_len);
                is (rc, 0, "msgpuck decomression");
                rc = memcmp(msgpuck, dmsgpuck, new_msgpuck_len);
                region_truncate(&fiber()->gc, used);
                msgpuck_random_delete(msgpuck);
                space_random_delete(space);
        }
        return check_plan();
}

int main()
{
        srand(time(NULL));
        memory_init();
        fiber_init(fiber_c_invoke);
        random_init();
        plan(1);
        check_random_msgpuck_compression_decompression();
        return check_plan();
}