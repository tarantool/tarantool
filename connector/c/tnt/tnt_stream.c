
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

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <connector/c/include/tarantool/tnt_mem.h>
#include <connector/c/include/tarantool/tnt_tuple.h>
#include <connector/c/include/tarantool/tnt_reply.h>
#include <connector/c/include/tarantool/tnt_stream.h>

/*
 * tnt_stream_reqid()
 *
 * set reqid value
 *
 * s     - stream pointer
 * reqid - new reqid value
 *
 * returns old reqid value
 *
*/
uint32_t
tnt_stream_reqid(struct tnt_stream *s, uint32_t reqid)
{
	uint32_t old = s->reqid;
	s->reqid = reqid;
	return old;
}

/*
 * tnt_stream_free()
 *
 * free stream object.
 *
 * s - stream pointer
 *
*/
void tnt_stream_free(struct tnt_stream *s) {
	if (s->free)
		s->free(s);
	if (s->alloc)
		tnt_mem_free(s);
}

