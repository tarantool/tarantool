#ifndef TNT_REQUEST_H_INCLUDED
#define TNT_REQUEST_H_INCLUDED

/*
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

#include <sys/types.h>
#include <sys/uio.h>

typedef ssize_t (*tnt_request_t)(void *ptr, char *dst, ssize_t size);

struct tnt_request_insert {
	struct tnt_header_insert h;
	struct tnt_tuple t;
};

struct tnt_request_delete_1_3 {
	struct tnt_header_delete_1_3 h;
	struct tnt_tuple t;
};

struct tnt_request_delete {
	struct tnt_header_delete h;
	struct tnt_tuple t;
};

struct tnt_request_update_op {
	uint8_t op;
	uint32_t field;
	char size_enc[5];
	uint32_t size_enc_len;
	uint32_t size;
	char *data;
};

struct tnt_request_update {
	struct tnt_header_update h;
	struct tnt_tuple t;
	char *ops;
	uint32_t ops_size;
	struct tnt_request_update_op *opv;
	uint32_t opc;
};

struct tnt_request_call {
	struct tnt_header_call h;
	char proc_enc[5];
	uint32_t proc_enc_len;
	char *proc;
	uint32_t proc_len;
	struct tnt_tuple t;
};

struct tnt_request_select {
	struct tnt_header_select h;
	struct tnt_list l;
};

struct tnt_request {
	char *origin;
	size_t origin_size;
	struct tnt_header h;
	union {
		struct tnt_request_insert insert;
		struct tnt_request_delete_1_3 del_1_3;
		struct tnt_request_delete del;
		struct tnt_request_call call;
		struct tnt_request_select select;
		struct tnt_request_update update;
	} r;
	int vc;
	struct iovec *v;
};

void tnt_request_init(struct tnt_request *r);
void tnt_request_free(struct tnt_request *r);
void tnt_request_setorigin(struct tnt_request *r, char *buf, size_t size);

int tnt_request(struct tnt_request *r, char *buf, size_t size, size_t *off,
		struct tnt_header *hdr);
int tnt_request_from(struct tnt_request *r, tnt_request_t rcv, void *ptr,
		     struct tnt_header *hdr);

#endif /* TNT_REQUEST_H_INCLUDED */
