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
#include "schema_def.h"

const char *sql_storage_engine_strs[] = {
	[SQL_STORAGE_ENGINE_MEMTX] = "memtx",
	[SQL_STORAGE_ENGINE_VINYL] = "vinyl",
};

static const char *object_type_strs[] = {
	/* [SC_UKNNOWN]         = */ "unknown",
	/* [SC_UNIVERSE]        = */ "universe",
	/* [SC_SPACE]           = */ "space",
	/* [SC_FUNCTION]        = */ "function",
	/* [SC_USER]            = */ "user",
	/* [SC_ROLE]            = */ "role",
	/* [SC_SEQUENCE]        = */ "sequence",
	/* [SC_COLLATION]       = */ "collation",
};

/** Given object type @type, return corresponding entity type. */
static enum schema_object_type
schema_object_type_to_entity(enum schema_object_type type)
{
	assert(type >= SC_SPACE);
	assert((int) type < (int) schema_object_type_MAX);
	return type + schema_object_type_MAX - 1;
}

/** Given entity type @type, return corresponding object type. */
static enum schema_object_type
schema_entity_type_to_object(enum schema_object_type type)
{
	assert((int) type > (int) schema_object_type_MAX);
	assert((int) type < (int) schema_entity_type_MAX);
	return (type % (schema_object_type_MAX)) + 1;
}

enum schema_object_type
schema_entity_type(enum schema_object_type type)
{
	if (type <= SC_UNIVERSE || type >= schema_object_type_MAX)
		return SC_UNKNOWN;
	return schema_object_type_to_entity(type);
}

enum schema_object_type
schema_object_type(const char *name)
{
	/**
	 * There may be other places in which we look object type by
	 * name, and they are case-sensitive, so be case-sensitive
	 * here too.
	 */
	int n_strs = sizeof(object_type_strs)/sizeof(*object_type_strs);
	int index = strindex(object_type_strs, name, n_strs);
	return (enum schema_object_type) (index == n_strs ? 0 : index);
}

const char *
schema_object_name(enum schema_object_type type)
{
	assert((int) type < (int) schema_object_type_MAX);
	return object_type_strs[type];
}

const char *
schema_entity_name(enum schema_object_type type)
{
	return object_type_strs[schema_entity_type_to_object(type)];
}
