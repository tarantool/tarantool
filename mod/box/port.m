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
#include "port.h"
#include <pickle.h>
#include <fiber.h>
#include <tarantool_lua.h>
#include "tuple.h"
#include "box_lua.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "lj_obj.h"
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_cconv.h"

/*
  For tuples of size below this threshold, when sending a tuple
  to the client, make a deep copy of the tuple for the duration
  of sending rather than increment a reference counter.
  This is necessary to avoid excessive page splits when taking
  a snapshot: many small tuples can be accessed by clients
  immediately after the snapshot process has forked off,
  thus incrementing tuple ref count, and causing the OS to
  create a copy of the memory page for the forked
  child.
*/
const int BOX_REF_THRESHOLD = 8196;

static void
tuple_unref(void *tuple)
{
	tuple_ref((struct tuple *) tuple, -1);
}

void
fiber_ref_tuple(struct tuple *tuple)
{
	tuple_ref(tuple, 1);
	fiber_register_cleanup(tuple_unref, tuple);
}

@implementation Port
- (void) addU32: (u32 *) p_u32
{
	[self subclassResponsibility: _cmd];
	(void) p_u32;
}
- (void) dupU32: (u32) u32
{
	[self subclassResponsibility: _cmd];
	(void) u32;
}
- (void) addTuple: (struct tuple *) tuple
{
	[self subclassResponsibility: _cmd];
	(void) tuple;
}
- (void) addLuaMultret: (struct lua_State *) L
{
	[self subclassResponsibility: _cmd];
	(void) L;
}
@end

@interface PortIproto: Port
@end

@implementation PortIproto

- (void) addU32: (u32 *) p_u32
{
	iov_add(p_u32, sizeof(u32));
}

- (void) dupU32: (u32) u32
{
	iov_dup(&u32, sizeof(u32));
}

- (void) addTuple: (struct tuple *) tuple
{
	size_t len = tuple_len(tuple);

	if (len > BOX_REF_THRESHOLD) {
		fiber_ref_tuple(tuple);
		iov_add(&tuple->bsize, len);
	} else {
		iov_dup(&tuple->bsize, len);
	}
}

/* Add a Lua table to iov as if it was a tuple, with as little
 * overhead as possible. */

static void
iov_add_lua_table(struct lua_State *L, int index)
{
	u32 *field_count = palloc(fiber->gc_pool, sizeof(u32));
	u32 *tuple_len = palloc(fiber->gc_pool, sizeof(u32));

	*field_count = 0;
	*tuple_len = 0;

	iov_add(tuple_len, sizeof(u32));
	iov_add(field_count, sizeof(u32));

	u8 field_len_buf[5];
	size_t field_len, field_len_len;
	const char *field;

	lua_pushnil(L);  /* first key */
	while (lua_next(L, index) != 0) {
		++*field_count;

		switch (lua_type(L, -1)) {
		case LUA_TNUMBER:
		{
			u32 field_num = lua_tonumber(L, -1);
			field_len = sizeof(u32);
			field_len_len =
				save_varint32(field_len_buf,
					      field_len) - field_len_buf;
			iov_dup(field_len_buf, field_len_len);
			iov_dup(&field_num, field_len);
			*tuple_len += field_len_len + field_len;
			break;
		}
		case LUA_TCDATA:
		{
			u64 field_num = tarantool_lua_tointeger64(L, -1);
			field_len = sizeof(u64);
			field_len_len =
				save_varint32(field_len_buf,
					      field_len) - field_len_buf;
			iov_dup(field_len_buf, field_len_len);
			iov_dup(&field_num, field_len);
			*tuple_len += field_len_len + field_len;
			break;
		}
		case LUA_TSTRING:
		{
			field = lua_tolstring(L, -1, &field_len);
			field_len_len =
				save_varint32(field_len_buf,
					      field_len) - field_len_buf;
			iov_dup(field_len_buf, field_len_len);
			iov_dup(field, field_len);
			*tuple_len += field_len_len + field_len;
			break;
		}
		default:
			tnt_raise(ClientError, :ER_PROC_RET,
				  lua_typename(L, lua_type(L, -1)));
			break;
		}
		lua_pop(L, 1);
	}
}

void iov_add_ret(struct lua_State *L, int index)
{
	int type = lua_type(L, index);
	struct tuple *tuple;
	switch (type) {
	case LUA_TTABLE:
	{
		iov_add_lua_table(L, index);
		return;
	}
	case LUA_TNUMBER:
	{
		size_t len = sizeof(u32);
		u32 num = lua_tointeger(L, index);
		tuple = tuple_alloc(len + varint32_sizeof(len));
		tuple->field_count = 1;
		memcpy(save_varint32(tuple->data, len), &num, len);
		break;
	}
	case LUA_TCDATA:
	{
		u64 num = tarantool_lua_tointeger64(L, index);
		size_t len = sizeof(u64);
		tuple = tuple_alloc(len + varint32_sizeof(len));
		tuple->field_count = 1;
		memcpy(save_varint32(tuple->data, len), &num, len);
		break;
	}
	case LUA_TSTRING:
	{
		size_t len;
		const char *str = lua_tolstring(L, index, &len);
		tuple = tuple_alloc(len + varint32_sizeof(len));
		tuple->field_count = 1;
		memcpy(save_varint32(tuple->data, len), str, len);
		break;
	}
	case LUA_TNIL:
	case LUA_TBOOLEAN:
	{
		const char *str = tarantool_lua_tostring(L, index);
		size_t len = strlen(str);
		tuple = tuple_alloc(len + varint32_sizeof(len));
		tuple->field_count = 1;
		memcpy(save_varint32(tuple->data, len), str, len);
		break;
	}
	case LUA_TUSERDATA:
	{
		tuple = lua_istuple(L, index);
		if (tuple)
			break;
	}
	default:
		/*
		 * LUA_TNONE, LUA_TTABLE, LUA_THREAD, LUA_TFUNCTION
		 */
		tnt_raise(ClientError, :ER_PROC_RET, lua_typename(L, type));
		break;
	}
	fiber_ref_tuple(tuple);
	iov_add(&tuple->bsize, tuple_len(tuple));
}

/**
 * Add all elements from Lua stack to fiber iov.
 *
 * To allow clients to understand a complex return from
 * a procedure, we are compatible with SELECT protocol,
 * and return the number of return values first, and
 * then each return value as a tuple.
 */
- (void) addLuaMultret: (struct lua_State *) L
{
	int nargs = lua_gettop(L);
	iov_dup(&nargs, sizeof(u32));
	for (int i = 1; i <= nargs; ++i)
		iov_add_ret(L, i);
}
@end


@implementation PortNull
- (void) addU32: (u32 *) p_u32                  { (void) p_u32; }
- (void) dupU32: (u32) u32	                { (void) u32; }
- (void) addTuple: (struct tuple *) tuple       { (void) tuple; }
- (void) addLuaMultret: (struct lua_State *) L  { (void) L; }
@end

Port *port_null;
Port *port_iproto;

void
port_init()
{
	port_iproto = [[PortIproto alloc] init];
	port_null = [[PortNull alloc] init];
}

void
port_free()
{
	if (port_iproto)
		[port_iproto free];
	if (port_null)
		[port_null free];
}
