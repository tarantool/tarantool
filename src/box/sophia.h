#ifndef SOPHIA_H_
#define SOPHIA_H_

/*
 * sophia database
 * sphia.org
 *
 * Copyright (c) Dmitry Simonenko
 * BSD License
*/

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>

#if __GNUC__ >= 4
#  define SP_API __attribute__((visibility("default")))
#else
#  define SP_API
#endif

SP_API void    *sp_env(void);
SP_API void    *sp_document(void*);
SP_API int      sp_setstring(void*, const char*, const void*, int);
SP_API int      sp_setint(void*, const char*, int64_t);
SP_API int      sp_setobject(void*, const char*, void*);
SP_API void    *sp_getobject(void*, const char*);
SP_API void    *sp_getstring(void*, const char*, int*);
SP_API int64_t  sp_getint(void*, const char*);
SP_API int      sp_open(void*);
SP_API int      sp_close(void*);
SP_API int      sp_drop(void*);
SP_API int      sp_destroy(void*);
SP_API int      sp_error(void*);
SP_API int      sp_service(void*);
SP_API void    *sp_poll(void*);
SP_API int      sp_set(void*, void*);
SP_API int      sp_upsert(void*, void*);
SP_API int      sp_delete(void*, void*);
SP_API void    *sp_get(void*, void*);
SP_API void    *sp_cursor(void*);
SP_API void    *sp_begin(void*);
SP_API int      sp_prepare(void*);
SP_API int      sp_commit(void*);

#ifdef __cplusplus
}
#endif

#endif
