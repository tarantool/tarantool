#ifndef INCLUDES_TARANTOOL_BOX_PORT_H
#define INCLUDES_TARANTOOL_BOX_PORT_H
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
#include <util.h>
#include <objc/Object.h>

struct tuple;
struct lua_State;

@interface Port: Object
- (void) addU32: (u32 *) u32;
- (void) dupU32: (u32) u32;
- (void) addTuple: (struct tuple *) tuple;
- (void) addLuaMultret: (struct lua_State *) L;
@end

@interface PortNull: Port
@end

/**
 * A hack to keep tuples alive until iov_flush(fiber->iovec).
 * Is internal to port_iproto implementation, but is also
 * used in memcached.m, which doesn't uses fiber->iovec
 * bypassing port_iproto a public declaration here.
 */
void fiber_ref_tuple(struct tuple *tuple);

/** These do not have state currently, thus a single
 * instance is sufficient.
 */
Port *port_null;
Port *port_iproto;

/** Init the subsystem. */
void port_init();
/** Stop the susbystem. */
void port_free();

#endif /* INCLUDES_TARANTOOL_BOX_PORT_H */
