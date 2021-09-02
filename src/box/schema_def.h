#ifndef TARANTOOL_BOX_SCHEMA_DEF_H_INCLUDED
#define TARANTOOL_BOX_SCHEMA_DEF_H_INCLUDED
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
#include "trivia/util.h"
#include <stdbool.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	BOX_ENGINE_MAX = 3, /* + 1 to the actual number of engines */
	BOX_SPACE_MAX = INT32_MAX,
	BOX_FUNCTION_MAX = 32000,
	BOX_INDEX_MAX = 128,
	BOX_NAME_MAX = 65000,
	BOX_INVALID_NAME_MAX = 64,
	ENGINE_NAME_MAX = 16,
	FIELD_TYPE_NAME_MAX = 16,
	GRANT_NAME_MAX = 16,
	BOX_FIELD_MAX = INT32_MAX,
	BOX_USER_MAX = 32,
	/**
	 * A fairly arbitrary limit which is still necessary
	 * to keep tuple_format object small.
	 */
	BOX_INDEX_FIELD_MAX = INT16_MAX,
	/** Yet another arbitrary limit which simply needs to
	 * exist.
	 */
	BOX_INDEX_PART_MAX = UINT8_MAX
};
static_assert(BOX_INVALID_NAME_MAX <= BOX_NAME_MAX,
	      "invalid name max is less than name max");

/** \cond public */
enum {
	/** Start of the reserved range of system spaces. */
	BOX_SYSTEM_ID_MIN = 256,
	/** Space if of _vinyl_deferred_delete. */
	BOX_VINYL_DEFERRED_DELETE_ID = 257,
	/** Space id of _schema. */
	BOX_SCHEMA_ID = 272,
	/** Space id of _collation. */
	BOX_COLLATION_ID = 276,
	/** Space id of _vcollation. */
	BOX_VCOLLATION_ID = 277,
	/** Space id of _space. */
	BOX_SPACE_ID = 280,
	/** Space id of _vspace view. */
	BOX_VSPACE_ID = 281,
	/** Space id of _sequence. */
	BOX_SEQUENCE_ID = 284,
	/** Space id of _sequence_data. */
	BOX_SEQUENCE_DATA_ID = 285,
	/** Space id of _vsequence view. */
	BOX_VSEQUENCE_ID = 286,
	/** Space id of _index. */
	BOX_INDEX_ID = 288,
	/** Space id of _vindex view. */
	BOX_VINDEX_ID = 289,
	/** Space id of _func. */
	BOX_FUNC_ID = 296,
	/** Space id of _vfunc view. */
	BOX_VFUNC_ID = 297,
	/** Space id of _user. */
	BOX_USER_ID = 304,
	/** Space id of _vuser view. */
	BOX_VUSER_ID = 305,
	/** Space id of _priv. */
	BOX_PRIV_ID = 312,
	/** Space id of _vpriv view. */
	BOX_VPRIV_ID = 313,
	/** Space id of _cluster. */
	BOX_CLUSTER_ID = 320,
	/** Space id of _trigger. */
	BOX_TRIGGER_ID = 328,
	/** Space id of _truncate. */
	BOX_TRUNCATE_ID = 330,
	/** Space id of _space_sequence. */
	BOX_SPACE_SEQUENCE_ID = 340,
	/** Space id of _fk_constraint. */
	BOX_FK_CONSTRAINT_ID = 356,
	/** Space id of _ck_contraint. */
	BOX_CK_CONSTRAINT_ID = 364,
	/** Space id of _func_index. */
	BOX_FUNC_INDEX_ID = 372,
	/** Space id of _session_settings. */
	BOX_SESSION_SETTINGS_ID = 380,
	/** End of the reserved range of system spaces. */
	BOX_SYSTEM_ID_MAX = 511,
	BOX_ID_NIL = 2147483647
};
/** \endcond public */

/** _space fields. */
enum {
	BOX_SPACE_FIELD_ID = 0,
	BOX_SPACE_FIELD_UID = 1,
	BOX_SPACE_FIELD_NAME = 2,
	BOX_SPACE_FIELD_ENGINE = 3,
	BOX_SPACE_FIELD_FIELD_COUNT = 4,
	BOX_SPACE_FIELD_OPTS = 5,
	BOX_SPACE_FIELD_FORMAT = 6,
	box_space_field_MAX = 7,
};

/** _index fields. */
enum {
	BOX_INDEX_FIELD_SPACE_ID = 0,
	BOX_INDEX_FIELD_ID = 1,
	BOX_INDEX_FIELD_NAME = 2,
	BOX_INDEX_FIELD_TYPE = 3,
	BOX_INDEX_FIELD_OPTS = 4,
	BOX_INDEX_FIELD_IS_UNIQUE_165 = 4,
	BOX_INDEX_FIELD_PARTS = 5,
	BOX_INDEX_FIELD_PART_COUNT_165 = 5,
	BOX_INDEX_FIELD_PARTS_165 = 6,
};

/** _user fields. */
enum {
	BOX_USER_FIELD_ID = 0,
	BOX_USER_FIELD_UID = 1,
	BOX_USER_FIELD_NAME = 2,
	BOX_USER_FIELD_TYPE = 3,
	BOX_USER_FIELD_AUTH_MECH_LIST = 4,
};

/** _priv fields. */
enum {
	BOX_PRIV_FIELD_ID = 0,
	BOX_PRIV_FIELD_UID = 1,
	BOX_PRIV_FIELD_OBJECT_TYPE = 2,
	BOX_PRIV_FIELD_OBJECT_ID = 3,
	BOX_PRIV_FIELD_ACCESS = 4,
};

/** _func fields. */
enum {
	BOX_FUNC_FIELD_ID = 0,
	BOX_FUNC_FIELD_UID = 1,
	BOX_FUNC_FIELD_NAME = 2,
	BOX_FUNC_FIELD_SETUID = 3,
	BOX_FUNC_FIELD_LANGUAGE = 4,
	BOX_FUNC_FIELD_BODY = 5,
	BOX_FUNC_FIELD_ROUTINE_TYPE = 6,
	BOX_FUNC_FIELD_PARAM_LIST = 7,
	BOX_FUNC_FIELD_RETURNS = 8,
	BOX_FUNC_FIELD_AGGREGATE = 9,
	BOX_FUNC_FIELD_SQL_DATA_ACCESS = 10,
	BOX_FUNC_FIELD_IS_DETERMINISTIC = 11,
	BOX_FUNC_FIELD_IS_SANDBOXED = 12,
	BOX_FUNC_FIELD_IS_NULL_CALL = 13,
	BOX_FUNC_FIELD_EXPORTS = 14,
	BOX_FUNC_FIELD_OPTS = 15,
	BOX_FUNC_FIELD_COMMENT = 16,
	BOX_FUNC_FIELD_CREATED = 17,
	BOX_FUNC_FIELD_LAST_ALTERED = 18,
};

/** _collation fields. */
enum {
	BOX_COLLATION_FIELD_ID = 0,
	BOX_COLLATION_FIELD_NAME = 1,
	BOX_COLLATION_FIELD_UID = 2,
	BOX_COLLATION_FIELD_TYPE = 3,
	BOX_COLLATION_FIELD_LOCALE = 4,
	BOX_COLLATION_FIELD_OPTIONS = 5,
};

/** _schema fields. */
enum {
	BOX_SCHEMA_FIELD_KEY = 0,
};

/** _cluster fields. */
enum {
	BOX_CLUSTER_FIELD_ID = 0,
	BOX_CLUSTER_FIELD_UUID = 1,
};

/** _truncate fields. */
enum {
	BOX_TRUNCATE_FIELD_SPACE_ID = 0,
	BOX_TRUNCATE_FIELD_COUNT = 1,
};

/** _sequence fields. */
enum {
	BOX_SEQUENCE_FIELD_ID = 0,
	BOX_SEQUENCE_FIELD_UID = 1,
	BOX_SEQUENCE_FIELD_NAME = 2,
	BOX_SEQUENCE_FIELD_STEP = 3,
	BOX_SEQUENCE_FIELD_MIN = 4,
	BOX_SEQUENCE_FIELD_MAX = 5,
	BOX_SEQUENCE_FIELD_START = 6,
	BOX_SEQUENCE_FIELD_CACHE = 7,
	BOX_SEQUENCE_FIELD_CYCLE = 8,
};

/** _sequence_data fields. */
enum {
	BOX_SEQUENCE_DATA_FIELD_ID = 0,
	BOX_SEQUENCE_DATA_FIELD_VALUE = 1,
};

/** _space_seq fields. */
enum {
	BOX_SPACE_SEQUENCE_FIELD_ID = 0,
	BOX_SPACE_SEQUENCE_FIELD_SEQUENCE_ID = 1,
	BOX_SPACE_SEQUENCE_FIELD_IS_GENERATED = 2,
	BOX_SPACE_SEQUENCE_FIELD_FIELDNO = 3,
	BOX_SPACE_SEQUENCE_FIELD_PATH = 4,
};

/** _trigger fields. */
enum {
	BOX_TRIGGER_FIELD_NAME = 0,
	BOX_TRIGGER_FIELD_SPACE_ID = 1,
	BOX_TRIGGER_FIELD_OPTS = 2,
};

/** _fk_constraint fields. */
enum {
	BOX_FK_CONSTRAINT_FIELD_NAME = 0,
	BOX_FK_CONSTRAINT_FIELD_CHILD_ID = 1,
	BOX_FK_CONSTRAINT_FIELD_PARENT_ID = 2,
	BOX_FK_CONSTRAINT_FIELD_DEFERRED = 3,
	BOX_FK_CONSTRAINT_FIELD_MATCH = 4,
	BOX_FK_CONSTRAINT_FIELD_ON_DELETE = 5,
	BOX_FK_CONSTRAINT_FIELD_ON_UPDATE = 6,
	BOX_FK_CONSTRAINT_FIELD_CHILD_COLS = 7,
	BOX_FK_CONSTRAINT_FIELD_PARENT_COLS = 8,
};

/** _ck_constraint fields. */
enum {
	BOX_CK_CONSTRAINT_FIELD_SPACE_ID = 0,
	BOX_CK_CONSTRAINT_FIELD_NAME = 1,
	BOX_CK_CONSTRAINT_FIELD_DEFERRED = 2,
	BOX_CK_CONSTRAINT_FIELD_LANGUAGE = 3,
	BOX_CK_CONSTRAINT_FIELD_CODE = 4,
	BOX_CK_CONSTRAINT_FIELD_IS_ENABLED = 5,
};

/** _func_index fields. */
enum {
	BOX_FUNC_INDEX_FIELD_SPACE_ID = 0,
	BOX_FUNC_INDEX_FIELD_INDEX_ID = 1,
	BOX_FUNC_INDEX_FUNCTION_ID = 2,
};

/** _session_settings fields. */
enum {
	BOX_SESSION_SETTINGS_FIELD_NAME = 0,
	BOX_SESSION_SETTINGS_FIELD_VALUE = 1,
};

/*
 * Different objects which can be subject to access
 * control.
 *
 * Use 0 for unknown to use the same index consistently
 * even when there are more object types in the future.
 */
enum schema_object_type {
	SC_UNKNOWN = 0,
	SC_UNIVERSE = 1,
	SC_SPACE = 2,
	SC_FUNCTION = 3,
	SC_USER = 4,
	SC_ROLE = 5,
	SC_SEQUENCE = 6,
	SC_COLLATION = 7,
	/*
	 * All object types are supposed to be above this point,
	 * all entity types - below.
	 */
	schema_object_type_MAX = 8,
	SC_ENTITY_SPACE,
	SC_ENTITY_FUNCTION,
	SC_ENTITY_USER,
	SC_ENTITY_ROLE,
	SC_ENTITY_SEQUENCE,
	SC_ENTITY_COLLATION,
	schema_entity_type_MAX = 15
};

/** SQL Storage engine. */
enum sql_storage_engine {
    SQL_STORAGE_ENGINE_MEMTX = 0,
    SQL_STORAGE_ENGINE_VINYL = 1,
    sql_storage_engine_MAX = 2
};

extern const char *sql_storage_engine_strs[];

/**
 * Given a object type, return an entity type it belongs to.
 */
enum schema_object_type
schema_entity_type(enum schema_object_type type);

enum schema_object_type
schema_object_type(const char *name);

const char *
schema_object_name(enum schema_object_type type);

const char *
schema_entity_name(enum schema_object_type type);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_SCHEMA_DEF_H_INCLUDED */
