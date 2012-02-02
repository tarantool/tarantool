#ifndef TNT_UPDATE_H_INCLUDED
#define TNT_UPDATE_H_INCLUDED

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

#define TNT_UPDATE_ASSIGN 0
#define TNT_UPDATE_ADD    1
#define TNT_UPDATE_AND    2
#define TNT_UPDATE_XOR    3
#define TNT_UPDATE_OR     4
#define TNT_UPDATE_SPLICE 5
#define TNT_UPDATE_DELETE 6

ssize_t
tnt_update_arith(struct tnt_stream *s, uint32_t field,
		 uint8_t op, uint32_t value);

ssize_t
tnt_update_assign(struct tnt_stream *s, uint32_t field,
		  char *data, uint32_t size);

ssize_t
tnt_update_splice(struct tnt_stream *s, uint32_t field,
		  uint32_t offset,
		  uint32_t length, char *data, size_t size);

ssize_t
tnt_update_delete(struct tnt_stream *s, uint32_t field);

ssize_t
tnt_update(struct tnt_stream *s, uint32_t ns, uint32_t flags,
	   struct tnt_tuple *k,
	   struct tnt_stream *ops);

#endif /* TNT_UPDATE_H_INCLUDED */
