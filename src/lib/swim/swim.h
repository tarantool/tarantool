#ifndef TARANTOOL_SWIM_H_INCLUDED
#define TARANTOOL_SWIM_H_INCLUDED
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
#include <stdbool.h>
#include <stdint.h>
#include "crypto/crypto.h"
#include "swim_constants.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct swim;
struct rlist;
struct tt_uuid;
struct swim_iterator;
struct swim_member;

/** Types of SWIM dead member deletion policy. */
enum swim_gc_mode {
	/** Just keep the current mode as is. */
	SWIM_GC_DEFAULT = -1,
	/**
	 * Turn GC off. With that mode dead members are never
	 * deleted automatically.
	 */
	SWIM_GC_OFF = 0,
	/**
	 * Turn GC on. Normal classical SWIM GC mode. Dead members
	 * are deleted automatically after a number of
	 * unacknowledged pings.
	 */
	SWIM_GC_ON = 1,
};

/**
 * Create a new SWIM instance. Do not bind to a port or set any
 * parameters. Allocation and initialization only. The function
 * yields.
 */
struct swim *
swim_new(uint64_t generation);

/** Check if a swim instance is configured. */
bool
swim_is_configured(const struct swim *swim);

/**
 * Configure or reconfigure a SWIM instance.
 *
 * @param swim SWIM instance to configure.
 * @param uri URI in the format "ip:port", or "port". In the
 *        latter case host is "127.0.0.1" by default.
 * @param heartbeat_rate Rate of sending round messages. It does
 *        not mean that each member will be checked each
 *        @heartbeat_rate seconds. It is rather the protocol
 *        speed. Protocol period depends on member count and
 *        @heartbeat_rate.
 * @param ack_timeout Time in seconds after which a ping is
 *        considered to be unacknowledged.
 * @param gc_mode Says if members should never be deleted due to
 *        too many unacknowledged pings. It could be useful, if
 *        SWIM is used mainly for monitoring of existing nodes
 *        with manual removal of dead ones, and probably with only
 *        a single initial discovery.
 * @param uuid UUID of this instance. Must be unique over the
 *        cluster.
 *
 * @retval 0 Success.
 * @retval -1 Error. Memory, not unique UUID, system error.
 */
int
swim_cfg(struct swim *swim, const char *uri, double heartbeat_rate,
	 double ack_timeout, enum swim_gc_mode gc_mode,
	 const struct tt_uuid *uuid);

/** Set payload to disseminate over the cluster. */
int
swim_set_payload(struct swim *swim, const char *payload, int payload_size);

/**
 * Set SWIM codec to encrypt/decrypt messages.
 * @param swim SWIM instance to set codec for.
 * @param algo Cipher algorithm.
 * @param mode Algorithm mode.
 * @param key Private key of the chosen algorithm. It is used to
 *        encrypt/decrypt messages, and should be the same on all
 *        cluster nodes. Note that it can be changed, but it shall
 *        be done on all cluster nodes. Otherwise the nodes will
 *        not understand each other. There is also a public key
 *        usually, but it is generated randomly inside SWIM.
 * @param key_size Key size in bytes.
 */
int
swim_set_codec(struct swim *swim, enum crypto_algo algo, enum crypto_mode mode,
	       const char *key, int key_size);

/**
 * Stop listening and broadcasting messages, cleanup all internal
 * structures, free memory. The function yields. Actual deletion
 * happens after currently working triggers are done.
 */
void
swim_delete(struct swim *swim);

/**
 * SWIM fd mainly is needed to be printed into the logs in order
 * to distinguish between different SWIM instances logs. And for
 * unit testing.
 */
int
swim_fd(const struct swim *swim);

/** Add a new member. */
int
swim_add_member(struct swim *swim, const char *uri, const struct tt_uuid *uuid);

/** Silently remove a member from member table. */
int
swim_remove_member(struct swim *swim, const struct tt_uuid *uuid);

/**
 * Send a ping to this address. If an ACK is received, the member
 * will be added. The main purpose of the method is to add a new
 * member manually but without knowledge of its UUID. The member
 * will send it with an ACK.
 */
int
swim_probe_member(struct swim *swim, const char *uri);

/**
 * Broadcast a ping to all interfaces supporting IPv4 with a
 * specified @a port.
 * @param swim SWIM instance to broadcast from.
 * @param port Port to send to. If < 0, then the current port of
 *        @a swim is used.
 *
 * @retval 0 Success. The broadcast packet was scheduled and at
 *         least one suitable interface was detected. But it does
 *         not mean, that the packet will be sent successfully.
 * @retval -1 Error. OOM, or there was not found an interface,
 *         supporting IPv4.
 */
int
swim_broadcast(struct swim *swim, int port);

/** Get SWIM member table size. */
int
swim_size(const struct swim *swim);

/**
 * Gracefully leave the cluster, broadcast a notification.
 * Members, received it, will remove a record about this instance
 * from their tables, and will not consider it dead. @a swim
 * object can not be used after quit and should be treated as
 * deleted.
 */
void
swim_quit(struct swim *swim);

/** Get a SWIM member, describing this instance. */
struct swim_member *
swim_self(struct swim *swim);

/**
 * Find a member by its UUID in the local member table.
 * @retval NULL Not found.
 * @retval not NULL A member.
 */
struct swim_member *
swim_member_by_uuid(struct swim *swim, const struct tt_uuid *uuid);

/** Member's current status. */
enum swim_member_status
swim_member_status(const struct swim_member *member);

/**
 * Open an iterator to scan the whole member table. The iterator
 * is not stable. It means, that a caller can not yield between
 * open and close - iterator position can be lost. Also it is
 * impossible to open more than one iterator on one SWIM instance
 * at the same time.
 */
struct swim_iterator *
swim_iterator_open(struct swim *swim);

/**
 * Get a next SWIM member.
 * @retval NULL EOF.
 * @retval not NULL A valid member.
 */
struct swim_member *
swim_iterator_next(struct swim_iterator *iterator);

/** Close an iterator. */
void
swim_iterator_close(struct swim_iterator *iterator);

/** Member's URI. */
const char *
swim_member_uri(const struct swim_member *member);

/** Member's UUID. */
const struct tt_uuid *
swim_member_uuid(const struct swim_member *member);

/** Member's incarnation. */
struct swim_incarnation
swim_member_incarnation(const struct swim_member *member);

/** Member's payload. */
const char *
swim_member_payload(const struct swim_member *member, int *size);

/**
 * Check if member's payload is up to its incarnation. Sometimes
 * it happens, that a member has changed payload, but other
 * members learned only a new incarnation without the new payload.
 * Then the payload is considered outdated, and is updated
 * eventually later. The method is rather internal, and should not
 * be used by any public API. It is exposed to implement decoded
 * payload cache in the SWIM Lua module.
 */
bool
swim_member_is_payload_up_to_date(const struct swim_member *member);

/**
 * Reference a member. The member memory will be valid until unref
 * is called.
 */
void
swim_member_ref(struct swim_member *member);

/**
 * Dereference a member. After that it may be deleted and can't be
 * accessed anymore.
 */
void
swim_member_unref(struct swim_member *member);

/**
 * Check if a member was dropped from the member table. It means,
 * that the member is not valid anymore and should be
 * dereferenced.
 */
bool
swim_member_is_dropped(const struct swim_member *member);

enum swim_ev_mask {
	SWIM_EV_NEW             = 0b00000001,
	SWIM_EV_NEW_STATUS      = 0b00000010,
	SWIM_EV_NEW_URI         = 0b00000100,
	SWIM_EV_NEW_GENERATION  = 0b00001000,
	SWIM_EV_NEW_VERSION     = 0b00010000,
	/*
	 * Shortcut to check for update of any part of
	 * incarnation.
	 */
	SWIM_EV_NEW_INCARNATION = 0b00011000,
	SWIM_EV_NEW_PAYLOAD     = 0b00100000,
	/* Shortcut to check for any update. */
	SWIM_EV_UPDATE          = 0b00111110,
	SWIM_EV_DROP            = 0b01000000,
};

/** On member event trigger context. */
struct swim_on_member_event_ctx {
	/** New, dropped, or updated member. */
	struct swim_member *member;
	/** Mask of happened events. */
	enum swim_ev_mask events;
};

/**
 * A list of member event processing triggers. A couple of words
 * about such a strange API, instead of something like
 * "add_trigger", "drop_trigger". A main motivation is that some
 * functions need a whole trigger list. For example, a function
 * adding Lua functions as triggers. At the time of this writing
 * it was lbox_trigger_reset. There is a convention, that any
 * Tarantool trigger exposed to Lua should provide a way to add
 * one, drop one, replace one, return all - lbox_trigger_reset
 * does all of this.
 */
struct rlist *
swim_trigger_list_on_member_event(struct swim *swim);

/**
 * Check if a SWIM instance has pending events. Is not a public
 * one, used by tests.
 */
bool
swim_has_pending_events(struct swim *swim);

#if defined(__cplusplus)
}
#endif

#endif /* TARANTOOL_SWIM_H_INCLUDED */
