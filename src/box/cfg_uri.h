#pragma once
/*
 * Copyright 2010-2021, Tarantool AUTHORS, please see AUTHORS file.
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

#if defined(__cplusplus)
extern "C" {
#endif

struct lua_State;

enum cfg_uri_options {
	CFG_URI_OPTION_BACKLOG = 0,
	CFG_URI_OPTION_READAHEAD,
        CFG_URI_OPTION_TRANSPORT,
	CFG_URI_OPTION_MAX,
};

struct cfg_uri_option {
        /**
         * Copy of URI option. We should work with copy of
         * appropriate string, because lua strings are constants.
         */
	char *copy;
        /**
         * Copy of URI option values strings.  We should work with
         * copy of appropriate string, because lua strings are constants.
         */
        char **values_copy;
        /** Size of 'values_copy' array */
        int values_copy_size;
        /** Name of URI option. */
	const char *name;
        /** Array of URI option values. */
	const char **values;
        /** Size of 'values' array. */
	int size;
};

struct cfg_uri {
        /** Pointer to a URI without options */
	const char *uri;
        /**
         * Copy of URI options. We should work with copy of
         * appropriate string, because lua strings are constants.
         */
	char *options_copy;
        /** Array of URI options */
	struct cfg_uri_option options[CFG_URI_OPTION_MAX];
};

/**
 * Array of structures, each of which contains URI and its options.
 * Also contains common URIs options, which related to all URIs.
 */
struct cfg_uri_array {
	/**
         * An array of strings that contains copies of all URI strings.
         * These strings can contain several different URIs along with
         * their options separated by commas. Since lua strings are
         * constants, we should make copies of them before splitting them
         * into parts.
         */
        char **copies;
        /** Size of 'copies' array. */
        int copies_size;
        /** Array of resulting URIs. */
	struct cfg_uri *uris;
        /** Size of 'uris' array */
	int size;
        /**
         * Storage of common URIs options, which related to all URIs.
         * This options are contained in the URI structure so that
         * the same functions can be used to get common options and
         * options specific to a particular URI.
         */
	struct cfg_uri common_options_storage;
};

/**
 * Creates @a array of structures, each of which contains URI and its options.
 * Expects that caller puts on the top of Lua stack a string or a table which
 * contains URI in a specific format.
 */
int
cfg_uri_array_create(struct cfg_uri_array *array, struct lua_State *L,
		     const char *cfg_option);

/** Destroys @a array and frees all associated resources. */
void
cfg_uri_array_destroy(struct cfg_uri_array *array);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
