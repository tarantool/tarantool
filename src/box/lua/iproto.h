/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */

#pragma once

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct lua_State;
struct mh_strnu32_t;

/**
 * Translation table for `box.iproto.key` constants encoding and aliasing: used
 * in `luamp_encode_with_translation` and `luamp_push_with_translation`.
 */
extern struct mh_strnu32_t *iproto_key_translation;

/**
 * Initializes `box.iproto` submodule for working with Tarantool network
 * subsystem.
 */
void
box_lua_iproto_init(struct lua_State *L);

/**
 * Frees `box.iproto` submodule.
 */
void
box_lua_iproto_free(void);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
