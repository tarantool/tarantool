#ifndef TNT_PROTO_H_INCLUDED
#define TNT_PROTO_H_INCLUDED

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

#define TNT_OP_INSERT      13
#define TNT_OP_SELECT      17
#define TNT_OP_UPDATE      19
#define TNT_OP_DELETE_1_3  20
#define TNT_OP_DELETE      21
#define TNT_OP_CALL        22
#define TNT_OP_PING        65280

#define TNT_FLAG_RETURN    0x01
#define TNT_FLAG_ADD       0x02
#define TNT_FLAG_REPLACE   0x04
#define TNT_FLAG_BOX_QUIET 0x08
#define TNT_FLAG_NOT_STORE 0x10

struct tnt_header {
	uint32_t type;
	uint32_t len;
	uint32_t reqid;
};

struct tnt_header_insert {
	uint32_t ns;
	uint32_t flags;
};

struct tnt_header_delete_1_3 {
	uint32_t ns;
};

struct tnt_header_delete {
	uint32_t ns;
	uint32_t flags;
};

struct tnt_header_update {
	uint32_t ns;
	uint32_t flags;
};

struct tnt_header_call {
	uint32_t flags;
};

struct tnt_header_select {
	uint32_t ns;
	uint32_t index;
	uint32_t offset;
	uint32_t limit;
};

#endif /* TNT_PROTO_H_INCLUDED */
