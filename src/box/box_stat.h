#ifndef INCLUDES_TARANTOOL_BOX_STAT_H
#define INCLUDES_TARANTOOL_BOX_STAT_H
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

#include "iproto_constants.h"
#include <limits.h>

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

/**
 * Statistics stuff
 */
enum box_call_type {
	/* Synonyms for iproto commands */
	BOX_OK = IPROTO_OK,
	BOX_SELECT = IPROTO_SELECT,
	BOX_INSERT = IPROTO_INSERT,
	BOX_REPLACE = IPROTO_REPLACE,
	BOX_UPDATE = IPROTO_UPDATE,
	BOX_DELETE = IPROTO_DELETE,
	BOX_CALL = IPROTO_CALL,
	BOX_AUTH = IPROTO_AUTH,
	BOX_EVAL = IPROTO_EVAL,
	/* Non-iproto stuff */
	BOX_EXCEPTION = BOX_EVAL + 1,
	BOX_STAT_MAX = BOX_EXCEPTION + 1
};
extern const char*box_type_strs[];
extern int stat_base;

#if defined(__cplusplus)
}
#endif /* defined(__cplusplus) */

#endif /* INCLUDES_TARANTOOL_BOX_STAT_H */
