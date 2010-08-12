/*
 * Copyright (C) 2010 Mail.RU
 * Copyright (C) 2010 Yuriy Vostrikov
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

#ifndef TARANTOOL_DEBUG_H
#define TARANTOOL_DEBUG_H

#ifdef HAVE_VALGRIND
#  include <third_party/valgrind/valgrind.h>
#  include <third_party/valgrind/memcheck.h>
#else
#  define VALGRIND_CREATE_MEMPOOL(a,b,c)
#  define VALGRIND_DESTROY_MEMPOOL(a)
#  define VALGRIND_MEMPOOL_TRIM(a,b,c)
#  define VALGRIND_MEMPOOL_ALLOC(a,b,c)
#  define VALGRIND_STACK_REGISTER(a,b)
#  define VALGRIND_MAKE_MEM_DEFINED(a,b)
#  define VALGRIND_MALLOCLIKE_BLOCK(a,b,c,d)
#  define VALGRIND_FREELIKE_BLOCK(a,b)
#  define VALGRIND_MAKE_MEM_UNDEFINED(a,b)
#endif
#endif
