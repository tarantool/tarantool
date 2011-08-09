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

/**
 * @defgroup Update
 * @ingroup Operations
 * @brief Update operation
 */
enum tnt_update_type {
	TNT_UPDATE_NONE,
	TNT_UPDATE_ASSIGN,
	TNT_UPDATE_ADD,
	TNT_UPDATE_AND,
	TNT_UPDATE_XOR,
	TNT_UPDATE_OR,
	TNT_UPDATE_SPLICE
};
/** @} */

struct tnt_update_op {
	uint8_t op;
	uint32_t field;
	char *data;
	uint32_t size;
	uint32_t size_leb;
	STAILQ_ENTRY(tnt_update_op) next;
};

struct tnt_update {
	uint32_t count;
	uint32_t size_enc;
	STAILQ_HEAD(,tnt_update_op) list;
};

/**
 * @defgroup Handler
 * @ingroup Update
 * @brief Update handler initizalization and operations 
 *
 * @{
 */
void tnt_update_init(struct tnt_update *update);
void tnt_update_free(struct tnt_update *update);

enum tnt_error
tnt_update_assign(struct tnt_update *update, int field,
		  char *value, int value_size);
enum tnt_error
tnt_update_arith(struct tnt_update *update, int field,
		 enum tnt_update_type op, int value);
enum tnt_error
tnt_update_splice(struct tnt_update *update, int field,
		  int offset, int length, char *list, int list_size);
/** @} */

/**
 * @defgroup Operation
 * @ingroup Update
 * @brief Update operation
 *
 * @{
 */

/**
 * Update operation with tuple.
 *
 * If bufferization is in use, then request would be placed in
 * internal buffer for later sending. Otherwise, operation
 * would be processed immediately.
 *
 * @param t handler pointer
 * @param reqid user supplied integer value
 * @param ns namespace number
 * @param flags update operation flags
 * @param key tuple object pointer
 * @param update update handler
 * @returns 0 on success, -1 on error
 */
int tnt_update_tuple(struct tnt *t, int reqid, int ns, int flags,
		     struct tnt_tuple *key, struct tnt_update *update);

/**
 * Update operation.
 *
 * If bufferization is in use, then request would be placed in
 * internal buffer for later sending. Otherwise, operation
 * would be processed immediately.
 *
 * @param t handler pointer
 * @param reqid user supplied integer value
 * @param ns namespace number
 * @param flags update operation flags
 * @param key key data
 * @param key key data size
 * @param update update handler
 * @returns 0 on success, -1 on error
 */
int tnt_update(struct tnt *t, int reqid, int ns, int flags,
	       char *key, int key_size, struct tnt_update *update);
/** @} */

#endif /* TNT_UPDATE_H_INCLUDED */
