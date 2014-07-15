#ifndef INCLUDES_TARANTOOL_MOD_BOX_LUA_CALL_H
#define INCLUDES_TARANTOOL_MOD_BOX_LUA_CALL_H
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

#include <stdint.h>

struct request;
struct port;

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
void
box_lua_call(struct request *request, struct port *port);

extern "C" {
struct port_ffi
{
	struct port_vtab *vtab;
	uint32_t size;
	uint32_t capacity;
	struct tuple **ret;
};

void
port_ffi_create(struct port_ffi *port);

void
port_ffi_destroy(struct port_ffi *port);

int
boxffi_select(struct port *port, uint32_t space_id, uint32_t index_id,
	      int iterator, uint32_t offset, uint32_t limit,
	      const char *key, const char *key_end);

} /* extern "C" */

#endif /* INCLUDES_TARANTOOL_MOD_BOX_LUA_CALL_H */
