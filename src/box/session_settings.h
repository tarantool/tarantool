#pragma once
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
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

/**
 * Session has settings. Settings belong to different subsystems,
 * such as SQL. Each subsystem registers here its session setting
 * type and a set of settings with getter and setter functions.
 * The self-registration of modules allows session setting code
 * not to depend on all the subsystems.
 *
 * The types should be ordered in alphabetical order, because the
 * type list is used by setting iterators.
 */
enum session_setting_type {
	session_setting_type_MAX,
};

struct session_setting_module {
	/**
	 * An array of setting names. All of them should have the
	 * same prefix.
	 */
	const char **settings;
	/** Count of settings. */
	int setting_count;
	/**
	 * Get a MessagePack encoded pair [name, value] for a
	 * setting having index @a id. Index is from the settings
	 * array.
	 */
	void (*get)(int id, const char **mp_pair, const char **mp_pair_end);
	/**
	 * Set value of a setting by a given @a id from a
	 * MessagePack encoded buffer. Note, it is not a pair, but
	 * just value.
	 */
	int (*set)(int id, const char *mp_value);
};

extern struct session_setting_module session_setting_modules[];
