#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <zlib.h>
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#define is_super(u) ((u)->roles.m[0] == 20u)
#define is_universe(s, l) ((l) == 0 || (s)[0] == '\0')
#define RT_ACCESS 1
#define RT_NO_ACCESS 0

const char *
user_name_by_id(uint32_t uid, uint32_t *name_len);

/**
 * Initialize the user access cache.
 */
void
rt_lua_call_access_init(void);

/**
 * Destroy the user access cache.
 */
void
rt_lua_call_access_destroy(void);

/**
 * Clear the user access cache.
 */
void
rt_lua_call_access_reset(void);

/**
 * Grant a access to lua_call function for a user.
 * @param uname Name of the user.
 * @param uname_len Length of the user name.
 * @param fname Name of the function.
 * @param fname_len Length of the function name.
 */
void
grant_rt_access(const char *uname, uint32_t uname_len,
		const char *fname, uint32_t fname_len);

/**
 * Revoke access to lua_call function for a user.
 * @param uname Name of the user.
 * @param uname_len Length of the user name.
 * @param name Name of the function.
 * @param name_len Length of the function name.
 */
void
revoke_rt_access(const char *uname, uint32_t uname_len,
		 const char *fname, uint32_t fname_len);

/**
 * Check if a user has a access to lua_call function.
 * @param uname Name of the user.
 * @param uname_len Length of the user name.
 * @param name Name of the function.
 * @param name_len Length of the function name.
 * @return 1 if the user has the privilege, 0 otherwise.
 */
int
check_rt_access(const char *uname, uint32_t uname_len,
		const char *fname, uint32_t fname_len);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
