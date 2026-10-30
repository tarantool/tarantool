#ifndef TARANTOOL_SWIM_CONSTANTS_H_INCLUDED
#define TARANTOOL_SWIM_CONSTANTS_H_INCLUDED
/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
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
/**
 * Constants for public API.
 */

enum swim_member_status {
	/** The instance is ok, responds to requests. */
	MEMBER_ALIVE = 0,
	/**
	 * If a member has not responded to a ping, it is declared
	 * as suspected to be dead. After more failed pings it
	 * is finally dead.
	 */
	MEMBER_SUSPECTED,
	/**
	 * The member is considered dead. It will disappear from
	 * the membership after some unacknowledged pings.
	 */
	MEMBER_DEAD,
	/** The member has voluntary left the cluster. */
	MEMBER_LEFT,
	swim_member_status_MAX,
};

extern const char *swim_member_status_strs[];

/**
 * A monotonically growing value to refute false gossips and
 * update member attributes on remote instances. Any piece of
 * information is labeled with an incarnation value. Information
 * labeled with a newer (bigger) incarnation is considered more
 * actual.
 */
struct swim_incarnation {
	/**
	 * Generation is a persistent part of incarnation. It is
	 * set by a user on SWIM start, and normally is not
	 * changed during instance lifetime.
	 */
	uint64_t generation;
	/**
	 * Version is a volatile part of incarnation. It is
	 * managed by SWIM fully internally.
	 */
	uint64_t version;
};

/** Create a new incarnation value. */
static inline void
swim_incarnation_create(struct swim_incarnation *i, uint64_t generation,
			uint64_t version)
{
	i->generation = generation;
	i->version = version;
}

/**
 * Compare two incarnation values.
 * @retval =0 l == r.
 * @retval <0 l < r.
 * @retval >0 l > r.
 */
int
swim_incarnation_cmp(const struct swim_incarnation *l,
		     const struct swim_incarnation *r);

#endif /* TARANTOOL_SWIM_CONSTANTS_H_INCLUDED */
