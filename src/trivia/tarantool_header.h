#ifndef TARANTOOL_MODULE_H_INCLUDED
#define TARANTOOL_MODULE_H_INCLUDED

#include <stddef.h>
#include <stdarg.h> /* va_list */
#include <errno.h>
#include <string.h> /* strerror(3) */
#include <stdint.h>
#include <sys/types.h> /* ssize_t */


/**
 * \file
 * Tarantool Module API
 */

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

#include <lua.h>  /* does not have extern C wrappers */
