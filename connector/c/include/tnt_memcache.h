#ifndef TNT_MEMCACHE_H_INCLUDED
#define TNT_MEMCACHE_H_INCLUDED

/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * @defgroup Memcache
 * @brief Memcache interface
 * @{
 */
int tnt_memcache_set(struct tnt *t, int flags, int expire, char *key,
		     char *data, int size);
int tnt_memcache_add(struct tnt *t, int flags, int expire, char *key,
		     char *data, int size);
int tnt_memcache_replace(struct tnt *t, int flags, int expire, char *key,
		         char *data, int size);
int tnt_memcache_append(struct tnt *t, int flags, int expire, char *key,
		        char *data, int size);
int tnt_memcache_prepend(struct tnt *t, int flags, int expire, char *key,
		         char *data, int size);
int tnt_memcache_cas(struct tnt *t, int flags, int expire,
		     unsigned long long cas, char *key,
		     char *data, int size);
int tnt_memcache_get(struct tnt *t, int cas, int count, char **keys,
		     struct tnt_memcache_vals * values);
int tnt_memcache_delete(struct tnt *t, char *key, int time);
int tnt_memcache_inc(struct tnt *t, char *key,
		     unsigned long long inc, unsigned long long *value);
int tnt_memcache_dec(struct tnt *t, char *key,
		     unsigned long long inc, unsigned long long *value);
int tnt_memcache_flush_all(struct tnt *t, int time);
/** @} */

#endif /* TNT_MEMCACHED_H_INCLUDED */
