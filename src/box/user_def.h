#ifndef TARANTOOL_BOX_USER_DEF_H_INCLUDED
#define TARANTOOL_BOX_USER_DEF_H_INCLUDED
/*
 * Copyright 2010-2015, Tarantool AUTHORS, please see AUTHORS file.
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
#include "key_def.h" /* for SCHEMA_OBJECT_TYPE */
#include "scramble.h" /* for SCRAMBLE_SIZE */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

enum {
	/* SELECT */
	PRIV_R = 1,
	/* INSERT, UPDATE, DELETE, REPLACE */
	PRIV_W = 2,
	/* CALL */
	PRIV_X = 4,
	/** Everything. */
	PRIV_ALL = PRIV_R + PRIV_W + PRIV_X
};

/* Privilege name for error messages */
const char *
priv_name(uint8_t access);

/**
 * A cache entry for an existing user. Entries for all existing
 * users are always present in the cache. The entry is maintained
 * in sync with _user and _priv system spaces by system space
 * triggers.
 * @sa alter.cc
 */
struct user_def {
	/** User id. */
	uint32_t uid;
	/** Creator of the user */
	uint32_t owner;
	/** 'user' or 'role' */
	enum schema_object_type type;
	/** User password - hash2 */
	char hash2[SCRAMBLE_SIZE];
	/** User name - for error messages and debugging */
	char name[BOX_NAME_MAX + 1];
};

/** Predefined user ids. */
enum { GUEST = 0, ADMIN =  1, PUBLIC = 2 /* role */ };

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_USER_DEF_H_INCLUDED */
