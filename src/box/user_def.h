#ifndef TARANTOOL_BOX_USER_DEF_H_INCLUDED
#define TARANTOOL_BOX_USER_DEF_H_INCLUDED
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
#include "schema_def.h" /* for SCHEMA_OBJECT_TYPE */
#include "scramble.h" /* for SCRAMBLE_SIZE */
#define RB_COMPACT 1
#include "small/rb.h"
#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * chap-sha1 of empty string, i.e. base64_encode(sha1(sha1(""), 0)
 */
extern const char *CHAP_SHA1_EMPTY_PASSWORD;

typedef uint16_t user_access_t;
/**
 * Effective session user. A cache of user data
 * and access stored in session and fiber local storage.
 * Differs from the authenticated user when executing
 * setuid functions.
 */
struct credentials {
	/** A look up key to quickly find session user. */
	uint8_t auth_token;
	/**
	 * Cached global grants, to avoid an extra look up
	 * when checking global grants.
	 */
	user_access_t universal_access;
	/** User id of the authenticated user. */
	uint32_t uid;
	/**
	 * Member of credentials list of the source user. The list
	 * is used to collect privilege updates to keep the
	 * credentials up to date.
	 */
	struct rlist in_user;
};

enum priv_type {
	/* SELECT */
	PRIV_R = 1,
	/* INSERT, UPDATE, UPSERT, DELETE, REPLACE */
	PRIV_W = 2,
	/* CALL */
	PRIV_X = 4,
	/* SESSION */
	PRIV_S = 8,
	/* USAGE */
	PRIV_U = 16,
	/* CREATE */
	PRIV_C = 32,
	/* DROP */
	PRIV_D = 64,
	/* ALTER */
	PRIV_A = 128,
	/* REFERENCE - required by ANSI - not implemented */
	PRIV_REFERENCE = 256,
	/* TRIGGER - required by ANSI - not implemented */
	PRIV_TRIGGER = 512,
	/* INSERT - required by ANSI - not implemented */
	PRIV_INSERT = 1024,
	/* UPDATE - required by ANSI - not implemented */
	PRIV_UPDATE = 2048,
	/* DELETE - required by ANSI - not implemented */
	PRIV_DELETE = 4096,
	/* This is never granted, but used internally. */
	PRIV_GRANT = 8192,
	/* Never granted, but used internally. */
	PRIV_REVOKE = 16384,
	/* all bits */
	PRIV_ALL  = ~((user_access_t) 0),
};

/**
 * Definition of a privilege
 */
struct priv_def {
	/** Who grants the privilege. */
	uint32_t grantor_id;
	/** Whom the privilege is granted. */
	uint32_t grantee_id;
	/* Object id - is only defined for object type */
	uint32_t object_id;
	/* Object type - function, space, universe */
	enum schema_object_type object_type;
	/**
	 * What is being granted, has been granted, or is being
	 * revoked.
	 */
	user_access_t access;
	/** To maintain a set of effective privileges. */
	rb_node(struct priv_def) link;
};

/* Privilege name for error messages */
const char *
priv_name(user_access_t access);

/**
 * Encapsulates privileges of a user on an object.
 * I.e. "space" object has an instance of this
 * structure for each user.
 */
struct access {
	/**
	 * Granted access has been given to a user explicitly
	 * via some form of a grant.
	 */
	user_access_t granted;
	/**
	 * Effective access is a sum of granted access and
	 * all privileges inherited by a user on this object
	 * via some role. Since roles may be granted to other
	 * roles, this may include indirect grants.
	 */
	user_access_t effective;
};

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
	char name[0];
};

static inline size_t
user_def_sizeof(uint32_t name_len)
{
	return sizeof(struct user_def) + name_len + 1;
}

/** Predefined user ids. */
enum {
	BOX_SYSTEM_USER_ID_MIN = 0,
	GUEST = 0,
	ADMIN =  1,
	PUBLIC = 2, /* role */
	SUPER = 31, /* role */
	BOX_SYSTEM_USER_ID_MAX = PUBLIC
};

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* TARANTOOL_BOX_USER_DEF_H_INCLUDED */
