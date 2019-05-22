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
#include "swim.h"
#include "swim_io.h"
#include "swim_proto.h"
#include "swim_ev.h"
#include "uri/uri.h"
#include "fiber.h"
#include "msgpuck.h"
#include "assoc.h"
#include "sio.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"

/**
 * SWIM - Scalable Weakly-consistent Infection-style Process Group
 * Membership Protocol. It consists of 2 components: events
 * dissemination and failure detection, and stores in memory a
 * table of known remote hosts - members. Also some SWIM
 * implementations have an additional component: anti-entropy -
 * periodical broadcast of a random subset of the member table.
 *
 * Each SWIM component is different in both protocol payload and
 * goals, and could even use different messages to send data. But
 * SWIM describes piggybacking of messages: a ping message can
 * piggyback a dissemination's one.
 *
 * SWIM has a main operating cycle during which it randomly
 * chooses members from a member table and sends to them events +
 * ping. Replies are processed out of the main cycle,
 * asynchronously.
 *
 * When a member unacknowledged too many pings, its status is
 * changed to 'suspected'. The SWIM paper describes suspicion
 * subcomponent as a protection against false-positive detection
 * of alive members as dead. It happens when a member is
 * overloaded and responds to pings too slow, or when the network
 * is in trouble and packets can not go through some channels.
 * When a member is suspected, another instance pings it
 * indirectly via other members. It sends a fixed number of pings
 * to the suspected one in parallel via additional hops selected
 * randomly among other members.
 *
 * Random selection in all the components provides even network
 * load of ~1 message on each member per one protocol step
 * regardless of the cluster size. Without randomness each member
 * would receive a network load of N messages in each protocol
 * step, where N is the cluster size.
 *
 * To speed up propagation of new information by means of a few
 * random messages SWIM proposes a kind of fairness: when
 * selecting a next random member to ping, the protocol prefers
 * LRU members. In code it would be too complicated, so
 * Tarantool's implementation is slightly different, easier:
 *
 * Tarantool splits protocol operation into rounds. At the
 * beginning of a round all members are randomly reordered and
 * linked into a list. At each round step a member is popped from
 * the list head, a message is sent to it, and then it waits for
 * the next round. In such implementation all random selection of
 * the original SWIM is executed once per round. The round is
 * 'planned', actually. A list is used instead of an array since
 * new members can be added to its tail without realloc, and dead
 * members can be removed easily as well.
 *
 * Also Tarantool implements the third SWIM component -
 * anti-entropy. Why is it needed and even vital? Consider the
 * example: two SWIM nodes, both are alive. Nothing happens, so
 * the events list is empty, only pings are being sent
 * periodically. Then a third node appears. It knows about one of
 * the existing nodes. How can it learn about the rest? Sure,
 * its known counterpart can try to notify its peer, but it is
 * UDP, so this event can be lost. Anti-entropy is an extra simple
 * component, it just piggybacks random part of the member table
 * with each regular message. In the example above the new node
 * will learn about the third one via anti-entropy messages from
 * the second one sooner or later.
 *
 * Surprisingly, original SWIM does not describe any addressing,
 * how to uniquely identify a member. IP/port fallaciously could
 * be considered as a good unique identifier, but some arguments
 * below demolish this belief:
 *
 *     - if instances work in separate containers, they can have
 *       the same IP/port inside a container NATed to a unique
 *       IP/port outside the container;
 *
 *     - IP/port are likely to change during instance lifecycle.
 *       Once IP/port are changed, a ghost of the old member's
 *       configuration still lives for a while until it is
 *       suspected, dead and GC-ed. Taking into account that ACK
 *       timeout can be tens of seconds, 'Dead Souls' can exist
 *       unpleasantly long.
 *
 * Tarantool SWIM implementation uses UUIDs as unique identifiers.
 * UUID is much more unlikely to change than IP/port. But even if
 * that happens, dissemination component for a while gossips the
 * new UUID together with the old one.
 *
 * SWIM implementation is split into 3 parts: protocol logic,
 * transport level, protocol structure.
 *
 *     - protocol logic consists of how to react on various
 *       events, failure detection pings/acks, how often to send
 *       messages, handles the logic of the three components
 *       (failure detection, anti-entropy, dissemination);
 *
 *     - transport level handles routing, transport headers,
 *       packet forwarding;
 *
 *     - protocol structure describes how packet looks in
 *       MessagePack, how sections and headers follow each other.
 */

enum {
	/**
	 * How often to send membership messages and pings in
	 * seconds. Nothing special in this concrete default
	 * value.
	 */
	HEARTBEAT_RATE_DEFAULT = 1,
	/**
	 * If a ping was sent, it is considered lost after this
	 * time without an ack. Nothing special in this value.
	 */
	ACK_TIMEOUT_DEFAULT = 30,
	/**
	 * If an alive member has not been responding to pings
	 * this number of times, it is suspected to be dead. To
	 * confirm the death it should fail more pings.
	 */
	NO_ACKS_TO_SUSPECT = 2,
	/**
	 * If a suspected member has not been responding to pings
	 * this number of times, it is considered dead. According
	 * to the SWIM paper, for a member it is sufficient to
	 * miss one direct ping, and an arbitrary but fixed number
	 * of simultaneous indirect pings, to be considered dead.
	 * Seems too little, so here it is bigger.
	 */
	NO_ACKS_TO_DEAD = 3,
	/**
	 * If a member is confirmed to be dead, it is removed from
	 * the member table after at least this number of
	 * unacknowledged pings. According to the SWIM paper, a
	 * dead member is deleted immediately. But we keep it for
	 * a while to 1) maybe refute its dead status,
	 * 2) disseminate the status via dissemination and
	 * anti-entropy components.
	 */
	NO_ACKS_TO_GC = 2,
	/**
	 * Number of pings sent indirectly to a member via other
	 * members when it did not answer on a regular ping. The
	 * messages are sent in parallel and via different
	 * members.
	 */
	INDIRECT_PING_COUNT = 2,
};

/**
 * Return a random number within given boundaries.
 *
 * Instead of blindly calculating a modulo, scale the random
 * number down the given boundaries to preserve the original
 * distribution. The result belongs to range [start, end].
 */
static inline int
swim_scaled_rand(int start, int end)
{
	assert(end >= start);
	/*
	 * RAND_MAX is likely to be INT_MAX - hardly SWIM will
	 * ever be used in such a huge cluster.
	 */
	assert(end - start < RAND_MAX);
	return rand() / (RAND_MAX / (end - start + 1) + 1);
}

/** Calculate UUID hash to use as a member table key. */
static inline uint32_t
swim_uuid_hash(const struct tt_uuid *uuid)
{
	return mh_strn_hash((const char *) uuid, UUID_LEN);
}

/**
 * A cluster member description. This structure describes the
 * last known state of an instance. This state is updated
 * periodically via UDP according to SWIM protocol rules.
 */
struct swim_member {
	/**
	 * Member status. Since the communication goes via UDP,
	 * actual status can be different, as well as different on
	 * other SWIM nodes. But SWIM guarantees that each member
	 * will learn a real status of an instance sometime.
	 */
	enum swim_member_status status;
	/**
	 * Address of the instance to which to send UDP packets.
	 */
	struct sockaddr_in addr;
	/**
	 * A unique identifier of the member. Is used as a key in
	 * the members table.
	 */
	struct tt_uuid uuid;
	/**
	 * Cached hash of the uuid for the member table lookups.
	 */
	uint32_t hash;
	/**
	 * Position in a queue of members in the current round.
	 */
	struct rlist in_round_queue;
	/**
	 * Reference counter. Used by public API to prevent the
	 * member deletion after it is obtained by UUID or from an
	 * iterator.
	 */
	int refs;
	/**
	 * True, if the member was dropped from the member table.
	 * At the same time it still can be not deleted, if users
	 * of the public API referenced the member. Dropped member
	 * is not valid anymore and should be dereferenced.
	 */
	bool is_dropped;
	/**
	 *
	 *                 Dissemination component
	 *
	 * Dissemination component sends events. Event is a
	 * notification about some member state update. The member
	 * maintains a different event type for each significant
	 * attribute - status, incarnation, etc not to send entire
	 * member state each time any member attribute changes.
	 *
	 * According to SWIM, an event should be sent to all
	 * members at least once - for that a TTD
	 * (time-to-disseminate) counter is maintained for each
	 * independent event type.
	 *
	 * When a member state changes, the TTD is reset to the
	 * cluster size. It is then decremented after each send.
	 * This guarantees that each member state change is sent
	 * to each SWIM member at least once. If a new event of
	 * the same type is generated before a round is finished,
	 * the current event object is updated in place with reset
	 * of the TTD.
	 *
	 * To conclude, TTD works in two ways: to see which
	 * specific member attribute needs dissemination and to
	 * track how many cluster members still need to learn
	 * about the change from this instance.
	 */
	/**
	 * General TTD reset each time when any visible member
	 * attribute is updated. It is always bigger or equal than
	 * any other TTDs. In addition it helps to keep a dead
	 * member not dropped until the TTD gets zero so as to
	 * allow other members to learn the dead status.
	 */
	int status_ttd;
	/** Arbitrary user data, disseminated on each change. */
	char *payload;
	/** Payload size, in bytes. */
	uint16_t payload_size;
	/**
	 * True, if the payload is thought to be of the most
	 * actual version. In such a case it can be disseminated
	 * further. Otherwise @a payload is suspected to be
	 * outdated and can be updated in two cases only:
	 *
	 * 1) when it is received with a bigger incarnation from
	 *    anywhere;
	 *
	 * 2) when it is received with the same incarnation, but
	 *    local payload is outdated.
	 *
	 * A payload can become outdated, if anyhow a new
	 * incarnation of the member has been learned, but not a
	 * new payload. For example, a message with new payload
	 * could be lost, and at the same time this instance
	 * responds to a ping with newly incarnated ack. The ack
	 * receiver will learn the new incarnation, but not the
	 * new payload.
	 *
	 * In this case it can't be said exactly whether the
	 * member has updated payload, or another attribute. The
	 * only way here is to wait until the most actual payload
	 * will be received from another instance. Note, that such
	 * an instance always exists - the payload originator
	 * instance.
	 */
	bool is_payload_up_to_date;
	/**
	 * TTD of payload. At most this number of times payload is
	 * sent as a part of dissemination component. Reset on
	 * each payload update.
	 */
	int payload_ttd;
	/**
	 * All created events are put into a queue sorted by event
	 * time.
	 */
	struct rlist in_dissemination_queue;
	/**
	 *
	 *               Failure detection component
	 */
	/**
	 * A monotonically growing number to refute old member's
	 * state, characterized by a triplet
	 * {incarnation, status, address}.
	 */
	uint64_t incarnation;
	/**
	 * How many recent pings did not receive an ack while the
	 * member was in the current status. When this number
	 * reaches a configured threshold the instance is marked
	 * as dead. After a few more unacknowledged it is removed
	 * from the member table. This counter is reset on each
	 * acknowledged ping, status or incarnation change.
	 */
	int unacknowledged_pings;
	/**
	 * A deadline when we stop expecting a response to the
	 * ping and account it as unacknowledged.
	 */
	double ping_deadline;
	/**
	 * Position in a queue of members waiting for an ack.
	 * A member is added to the queue when we send a ping
	 * message to it.
	 */
	struct heap_node in_wait_ack_heap;
	/** Ready at hand regular ACK task. */
	struct swim_task ack_task;
	/** Ready at hand regular PING task. */
	struct swim_task ping_task;
};

#define mh_name _swim_table
struct mh_swim_table_key {
	uint32_t hash;
	const struct tt_uuid *uuid;
};
#define mh_key_t struct mh_swim_table_key
#define mh_node_t struct swim_member *
#define mh_arg_t void *
#define mh_hash(a, arg) ((*a)->hash)
#define mh_hash_key(a, arg) (a.hash)
#define mh_cmp(a, b, arg) (tt_uuid_compare(&(*a)->uuid, &(*b)->uuid))
#define mh_cmp_key(a, b, arg) (tt_uuid_compare(a.uuid, &(*b)->uuid))
#define MH_SOURCE 1
#include "salad/mhash.h"

#define HEAP_NAME wait_ack_heap
#define HEAP_LESS(h, a, b) ((a)->ping_deadline < (b)->ping_deadline)
#define heap_value_t struct swim_member
#define heap_value_attr in_wait_ack_heap
#include "salad/heap.h"

/**
 * SWIM instance. Stores configuration, manages periodical tasks,
 * rounds. Each member has an object of this type on its host,
 * while on others it is represented as a struct swim_member
 * object.
 */
struct swim {
	/**
	 * Global hash of all known members of the cluster. Hash
	 * key is UUID, value is a struct member, describing a
	 * remote instance. Discovered members live here until
	 * they are detected as dead - in such a case they are
	 * removed from the hash after a while.
	 */
	struct mh_swim_table_t *members;
	/**
	 * This node. Is used to not send messages to self, it's
	 * meaningless. Also to refute false gossips about self
	 * status.
	 */
	struct swim_member *self;
	/**
	 * Scheduler of output requests, receiver of incoming
	 * ones.
	 */
	struct swim_scheduler scheduler;
	/**
	 * An offset of this instance in the hash table. Is used
	 * to iterate in the hash table starting from this swim
	 * instance. Such iteration is unstable between yields
	 * (i.e. member positions may change when the table is
	 * resized after an incoming event), but still is useful
	 * for a fast non-yielding scan of the member table
	 * starting from this instance.
	 */
	mh_int_t iterator;
	/**
	 *
	 *               Failure detection component
	 */
	/**
	 * A heap of members waiting for an ACK. A member is added
	 * to the queue when a ping is sent, and is removed from
	 * the queue when an ACK is received or a timeout expires.
	 * The heap is sorted by ping deadline in ascending order
	 * (bottom is farther in the future, top is closer to now
	 * or is in the past).
	 */
	heap_t wait_ack_heap;
	/** Generator of ack checking events. */
	struct ev_timer wait_ack_tick;
	/** GC state saying how to remove dead members. */
	enum swim_gc_mode gc_mode;
	/**
	 *
	 *                 Dissemination component
	 */
	/**
	 * Queue of all members which have dissemination
	 * information. A member is added to the queue whenever
	 * any of its attributes changes, and stays in the queue
	 * as long as the event TTD is non-zero.
	 */
	struct rlist dissemination_queue;
	/**
	 * Members to which a message should be sent next during
	 * this round.
	 */
	struct rlist round_queue;
	/** Generator of round step events. */
	struct ev_timer round_tick;
	/**
	 * True if a packet in the round step task is still valid
	 * and can be resent on a next round step.
	 */
	bool is_round_packet_valid;
	/**
	 * Preallocated buffer to store shuffled members here at
	 * the beginning of each round.
	 */
	struct swim_member **shuffled;
	/**
	 * Single round step task. It is impossible to have
	 * multiple round steps in the same SWIM instance at the
	 * same time, so it is single and preallocated per SWIM
	 * instance. Note, that the task's packet once built at
	 * the beginning of a round is reused during the round
	 * without rebuilding on each step. But packet rebuild can
	 * be triggered by any update of any member.
	 *
	 * Keep this structure at the bottom - it is huge and
	 * should not split other attributes into different cache
	 * lines.
	 */
	struct swim_task round_step_task;
};

/**
 * Mark cached round message invalid on any change of any member.
 * It triggers postponed rebuilding of the message. The round
 * packet can not be rebuilt right now because 1) invalidation can
 * occur several times in row when multiple member attributes are
 * updated, or more than one member are added, 2) the message can
 * be in fly right now in the output queue inside the scheduler.
 */
static inline void
swim_cached_round_msg_invalidate(struct swim *swim)
{
	swim->is_round_packet_valid = false;
}

/** Put the member into a list of ACK waiters. */
static void
swim_wait_ack(struct swim *swim, struct swim_member *member,
	      bool was_ping_indirect)
{
	if (heap_node_is_stray(&member->in_wait_ack_heap)) {
		double timeout = swim->wait_ack_tick.repeat;
		/*
		 * Direct ping is two trips: PING + ACK.
		 * Indirect ping is four trips: PING,
		 * FORWARD PING, ACK, FORWARD ACK. This is why x2
		 * for indirects.
		 */
		if (was_ping_indirect)
			timeout *= 2;
		member->ping_deadline = swim_time() + timeout;
		wait_ack_heap_insert(&swim->wait_ack_heap, member);
		swim_ev_timer_again(loop(), &swim->wait_ack_tick);
	}
}

/**
 * On literally any update of a member it is added to a queue of
 * members to disseminate updates. Regardless of other TTDs, each
 * update also resets status TTD. Status TTD is always greater
 * than any other event-related TTD, so it's sufficient to look at
 * it alone to see that a member needs information dissemination.
 * The status change itself occupies only 2 bytes in a packet, so
 * it is cheap to send it on any update, while does reduce
 * entropy.
 */
static inline void
swim_register_event(struct swim *swim, struct swim_member *member)
{
	if (rlist_empty(&member->in_dissemination_queue)) {
		rlist_add_tail_entry(&swim->dissemination_queue, member,
				     in_dissemination_queue);
	}
	member->status_ttd = mh_size(swim->members);
	swim_cached_round_msg_invalidate(swim);
}

/**
 * Make all needed actions to process a member's update like a
 * change of its status, or incarnation, or both.
 */
static void
swim_on_member_update(struct swim *swim, struct swim_member *member)
{
	member->unacknowledged_pings = 0;
	swim_register_event(swim, member);
}

/**
 * Update status and incarnation of the member if needed. Statuses
 * are compared as a compound key: {incarnation, status}. So @a
 * new_status can override an old one only if its incarnation is
 * greater, or the same, but its status is "bigger". Statuses are
 * compared by their identifier, so "alive" < "dead". This
 * protects from the case when a member is detected as dead on one
 * instance, but overridden by another instance with the same
 * incarnation's "alive" message.
 */
static inline void
swim_update_member_inc_status(struct swim *swim, struct swim_member *member,
			      enum swim_member_status new_status,
			      uint64_t incarnation)
{
	/*
	 * Source of truth about self is this instance and it is
	 * never updated from remote. Refutation is handled
	 * separately.
	 */
	assert(member != swim->self);
	if (member->incarnation < incarnation) {
		member->status = new_status;
		member->incarnation = incarnation;
		swim_on_member_update(swim, member);
	} else if (member->incarnation == incarnation &&
		   member->status < new_status) {
		member->status = new_status;
		swim_on_member_update(swim, member);
	}
}

int
swim_fd(const struct swim *swim)
{
	return swim->scheduler.transport.fd;
}

/**
 * A helper to get a pointer to a SWIM instance having only a
 * pointer to its scheduler. It is used by task complete
 * functions.
 */
static inline struct swim *
swim_by_scheduler(struct swim_scheduler *scheduler)
{
	return container_of(scheduler, struct swim, scheduler);
}

/** Update member's payload, register a corresponding event. */
static inline int
swim_update_member_payload(struct swim *swim, struct swim_member *member,
			   const char *payload, uint16_t payload_size)
{
	assert(payload_size <= MAX_PAYLOAD_SIZE);
	char *new_payload;
	if (payload_size > 0) {
		new_payload = (char *) realloc(member->payload, payload_size);
		if (new_payload == NULL) {
			diag_set(OutOfMemory, payload_size, "realloc", "new_payload");
			return -1;
		}
		memcpy(new_payload, payload, payload_size);
	} else {
		free(member->payload);
		new_payload = NULL;
	}
	member->payload = new_payload;
	member->payload_size = payload_size;
	member->payload_ttd = mh_size(swim->members);
	member->is_payload_up_to_date = true;
	swim_on_member_update(swim, member);
	return 0;
}

/**
 * Once a ping is sent, the member should start waiting for an
 * ACK.
 */
static void
swim_ping_task_complete(struct swim_task *task,
			struct swim_scheduler *scheduler, int rc)
{
	/*
	 * If ping send has failed, it makes no sense to wait for
	 * an ACK.
	 */
	if (rc < 0)
		return;
	struct swim *swim = swim_by_scheduler(scheduler);
	struct swim_member *m = container_of(task, struct swim_member,
					     ping_task);
	swim_wait_ack(swim, m, false);
}

void
swim_member_ref(struct swim_member *member)
{
	++member->refs;
}

void
swim_member_unref(struct swim_member *member)
{
	assert(member->refs > 0);
	if (--member->refs == 0) {
		free(member->payload);
		free(member);
	}
}

bool
swim_member_is_dropped(const struct swim_member *member)
{
	return member->is_dropped;
}

/** Free member's resources. */
static inline void
swim_member_delete(struct swim_member *member)
{
	assert(rlist_empty(&member->in_round_queue));
	member->is_dropped = true;

	/* Failure detection component. */
	assert(heap_node_is_stray(&member->in_wait_ack_heap));
	swim_task_destroy(&member->ack_task);
	swim_task_destroy(&member->ping_task);

	/* Dissemination component. */
	assert(rlist_empty(&member->in_dissemination_queue));

	swim_member_unref(member);
}

/** Create a new member. It is not registered anywhere here. */
static struct swim_member *
swim_member_new(const struct sockaddr_in *addr, const struct tt_uuid *uuid,
		enum swim_member_status status, uint64_t incarnation)
{
	struct swim_member *member =
		(struct swim_member *) calloc(1, sizeof(*member));
	if (member == NULL) {
		diag_set(OutOfMemory, sizeof(*member), "calloc", "member");
		return NULL;
	}
	member->refs = 1;
	member->status = status;
	member->addr = *addr;
	member->uuid = *uuid;
	member->hash = swim_uuid_hash(uuid);
	rlist_create(&member->in_round_queue);

	/* Failure detection component. */
	member->incarnation = incarnation;
	heap_node_create(&member->in_wait_ack_heap);
	swim_task_create(&member->ack_task, NULL, NULL, "ack");
	swim_task_create(&member->ping_task, swim_ping_task_complete, NULL,
			 "ping");

	/* Dissemination component. */
	rlist_create(&member->in_dissemination_queue);

	return member;
}

/**
 * Remove the member from all queues, hashes, destroy it and free
 * the memory.
 */
static void
swim_delete_member(struct swim *swim, struct swim_member *member)
{
	say_verbose("SWIM %d: member %s is deleted", swim_fd(swim),
		    tt_uuid_str(&member->uuid));
	struct mh_swim_table_key key = {member->hash, &member->uuid};
	mh_int_t rc = mh_swim_table_find(swim->members, key, NULL);
	assert(rc != mh_end(swim->members));
	mh_swim_table_del(swim->members, rc, NULL);
	swim_cached_round_msg_invalidate(swim);
	rlist_del_entry(member, in_round_queue);

	/* Failure detection component. */
	if (! heap_node_is_stray(&member->in_wait_ack_heap))
		wait_ack_heap_delete(&swim->wait_ack_heap, member);

	/* Dissemination component. */
	rlist_del_entry(member, in_dissemination_queue);

	swim_member_delete(member);
}

/** Find a member by UUID. */
static inline struct swim_member *
swim_find_member(struct swim *swim, const struct tt_uuid *uuid)
{
	struct mh_swim_table_key key = {swim_uuid_hash(uuid), uuid};
	mh_int_t node = mh_swim_table_find(swim->members, key, NULL);
	if (node == mh_end(swim->members))
		return NULL;
	return *mh_swim_table_node(swim->members, node);
}

/**
 * Register a new member with a specified status. It is not added
 * to the round queue here. It waits until the current round is
 * finished, and then is included into a new round. It is done
 * mainly to not add self into the round queue, because self is
 * also created via this function.
 */
static struct swim_member *
swim_new_member(struct swim *swim, const struct sockaddr_in *addr,
		const struct tt_uuid *uuid, enum swim_member_status status,
		uint64_t incarnation, const char *payload, int payload_size)
{
	int new_bsize = sizeof(swim->shuffled[0]) *
			(mh_size(swim->members) + 1);
	struct swim_member **new_shuffled =
		(struct swim_member **) realloc(swim->shuffled, new_bsize);
	if (new_shuffled == NULL) {
		diag_set(OutOfMemory, new_bsize, "realloc", "new_shuffled");
		return NULL;
	}
	swim->shuffled = new_shuffled;
	/*
	 * Reserve one more slot to never fail push into the ack
	 * waiters heap.
	 */
	if (wait_ack_heap_reserve(&swim->wait_ack_heap) != 0) {
		diag_set(OutOfMemory, sizeof(struct heap_node), "realloc",
			 "wait_ack_heap");
		return NULL;
	}
	struct swim_member *member =
		swim_member_new(addr, uuid, status, incarnation);
	if (member == NULL)
		return NULL;
	assert(swim_find_member(swim, uuid) == NULL);
	mh_int_t rc = mh_swim_table_put(swim->members,
					(const struct swim_member **) &member,
					NULL, NULL);
	if (rc == mh_end(swim->members)) {
		swim_member_delete(member);
		diag_set(OutOfMemory, sizeof(mh_int_t), "malloc", "node");
		return NULL;
	}
	if (mh_size(swim->members) > 1)
		swim_ev_timer_again(loop(), &swim->round_tick);

	/* Dissemination component. */
	swim_on_member_update(swim, member);
	if (payload_size >= 0 &&
	    swim_update_member_payload(swim, member, payload,
				       payload_size) != 0) {
		swim_delete_member(swim, member);
		return NULL;
	}

	say_verbose("SWIM %d: member %s is added, total is %d", swim_fd(swim),
		    tt_uuid_str(&member->uuid), mh_size(swim->members));
	return member;
}

/**
 * Take all the members from the table and shuffle them randomly.
 * Is used for forthcoming round planning.
 */
static void
swim_shuffle_members(struct swim *swim)
{
	struct mh_swim_table_t *members = swim->members;
	int i = 0;
	/*
	 * This shuffling preserves even distribution of a random
	 * sequence. The distribution properties have been
	 * verified by a longevity test.
	 */
	for (mh_int_t node = mh_first(members), end = mh_end(members);
	     node != end; node = mh_next(members, node), ++i) {
		swim->shuffled[i] = *mh_swim_table_node(members, node);
		int j = swim_scaled_rand(0, i);
		SWAP(swim->shuffled[i], swim->shuffled[j]);
	}
}

/**
 * Shuffle members, build randomly ordered queue of addressees. In
 * other words, do all round preparation work.
 */
static void
swim_new_round(struct swim *swim)
{
	int size = mh_size(swim->members);
	if (size == 1) {
		assert(swim->self != NULL);
		say_verbose("SWIM %d: skip a round - no members",
			    swim_fd(swim));
		return;
	}
	/* -1 for self. */
	say_verbose("SWIM %d: start a new round with %d members", swim_fd(swim),
		    size - 1);
	swim_cached_round_msg_invalidate(swim);
	swim_shuffle_members(swim);
	rlist_create(&swim->round_queue);
	for (int i = 0; i < size; ++i) {
		if (swim->shuffled[i] != swim->self) {
			rlist_add_entry(&swim->round_queue, swim->shuffled[i],
					in_round_queue);
		}
	}
}

/**
 * Encode one member into @a packet using @a passport structure.
 * Note that this function does not make a decision whether
 * payload should be encoded, because its callers have different
 * conditions for that. The anti-entropy needs the payload be
 * up-to-date. The dissemination component additionally needs
 * TTD > 0.
 * @retval 0 Success, encoded.
 * @retval -1 Not enough memory in the packet.
 */
static int
swim_encode_member(struct swim_packet *packet, struct swim_member *m,
		   struct swim_passport_bin *passport,
		   struct swim_member_payload_bin *payload_header,
		   bool encode_payload)
{
	/* The headers should be initialized. */
	assert(passport->k_status == SWIM_MEMBER_STATUS);
	assert(payload_header->k_payload == SWIM_MEMBER_PAYLOAD);
	int size = sizeof(*passport);
	encode_payload = encode_payload && m->is_payload_up_to_date;
	if (encode_payload)
		size += sizeof(*payload_header) + m->payload_size;
	char *pos = swim_packet_alloc(packet, size);
	if (pos == NULL)
		return -1;
	swim_passport_bin_fill(passport, &m->addr, &m->uuid, m->status,
			       m->incarnation, encode_payload);
	memcpy(pos, passport, sizeof(*passport));
	if (encode_payload) {
		pos += sizeof(*passport);
		swim_member_payload_bin_fill(payload_header, m->payload_size);
		memcpy(pos, payload_header, sizeof(*payload_header));
		pos += sizeof(*payload_header);
		memcpy(pos, m->payload, m->payload_size);
	}
	return 0;
}

/**
 * Encode anti-entropy header and random members data as many as
 * possible to the end of the packet.
 * @retval Number of key-values added to the packet's root map.
 */
static int
swim_encode_anti_entropy(struct swim *swim, struct swim_packet *packet)
{
	struct swim_anti_entropy_header_bin ae_header_bin;
	struct swim_passport_bin passport_bin;
	struct swim_member_payload_bin payload_header;
	char *header = swim_packet_alloc(packet, sizeof(ae_header_bin));
	if (header == NULL)
		return 0;
	swim_passport_bin_create(&passport_bin);
	swim_member_payload_bin_create(&payload_header);
	struct mh_swim_table_t *t = swim->members;
	int i = 0, member_count = mh_size(t);
	int rnd = swim_scaled_rand(0, member_count - 1);
	for (mh_int_t rc = mh_swim_table_random(t, rnd), end = mh_end(t);
	     i < member_count; ++i) {
		struct swim_member *m = *mh_swim_table_node(t, rc);
		if (swim_encode_member(packet, m, &passport_bin,
				       &payload_header, true) != 0)
			break;
		/*
		 * First random member could be chosen too close
		 * to the hash end. Here the cycle is wrapped, if
		 * a packet still has free memory, but the
		 * iterator has already reached the hash end.
		 */
		rc = mh_next(t, rc);
		if (rc == end)
			rc = mh_first(t);
	}
	swim_anti_entropy_header_bin_create(&ae_header_bin, i);
	memcpy(header, &ae_header_bin, sizeof(ae_header_bin));
	return 1;
}

/**
 * Encode source UUID.
 * @retval Number of key-values added to the packet's root map.
 */
static inline int
swim_encode_src_uuid(struct swim *swim, struct swim_packet *packet)
{
	struct swim_src_uuid_bin uuid_bin;
	char *pos = swim_packet_alloc(packet, sizeof(uuid_bin));
	if (pos == NULL)
		return 0;
	swim_src_uuid_bin_create(&uuid_bin, &swim->self->uuid);
	memcpy(pos, &uuid_bin, sizeof(uuid_bin));
	return 1;
}

/**
 * Encode failure detection component.
 * @retval Number of key-values added to the packet's root map.
 */
static int
swim_encode_failure_detection(struct swim *swim, struct swim_packet *packet,
			      enum swim_fd_msg_type type)
{
	struct swim_fd_header_bin fd_header_bin;
	int size = sizeof(fd_header_bin);
	char *pos = swim_packet_alloc(packet, size);
	if (pos == NULL)
		return 0;
	swim_fd_header_bin_create(&fd_header_bin, type,
				  swim->self->incarnation);
	memcpy(pos, &fd_header_bin, size);
	return 1;
}

/**
 * Encode dissemination component.
 * @retval Number of key-values added to the packet's root map.
 */
static int
swim_encode_dissemination(struct swim *swim, struct swim_packet *packet)
{
	struct swim_diss_header_bin diss_header_bin;
	struct swim_member_payload_bin payload_header;
	struct swim_passport_bin passport_bin;
	char *header = swim_packet_alloc(packet, sizeof(diss_header_bin));
	if (header == NULL)
		return 0;
	swim_passport_bin_create(&passport_bin);
	swim_member_payload_bin_create(&payload_header);
	int i = 0;
	struct swim_member *m;
	rlist_foreach_entry(m, &swim->dissemination_queue,
			    in_dissemination_queue) {
		if (swim_encode_member(packet, m, &passport_bin,
				       &payload_header,
				       m->payload_ttd > 0) != 0)
			break;
		++i;
	}
	swim_diss_header_bin_create(&diss_header_bin, i);
	memcpy(header, &diss_header_bin, sizeof(diss_header_bin));
	return 1;
}

/** Encode SWIM components into a UDP packet. */
static void
swim_encode_round_msg(struct swim *swim)
{
	if (swim->is_round_packet_valid)
		return;
	struct swim_packet *packet = &swim->round_step_task.packet;
	swim_packet_create(packet);
	char *header = swim_packet_alloc(packet, 1);
	int map_size = 0;
	map_size += swim_encode_src_uuid(swim, packet);
	map_size += swim_encode_failure_detection(swim, packet,
						  SWIM_FD_MSG_PING);
	map_size += swim_encode_dissemination(swim, packet);
	map_size += swim_encode_anti_entropy(swim, packet);

	assert(mp_sizeof_map(map_size) == 1 && map_size >= 2);
	mp_encode_map(header, map_size);
	swim->is_round_packet_valid = true;
}

/**
 * Decrement TTDs of all events. It is done after each round step.
 * Note, since we decrement TTD of all events, even those which
 * have not been actually encoded and sent, if there are more
 * events than can fit into a packet, the tail of the queue begins
 * reeking and rotting. The most recently added members could even
 * be deleted without being sent once. This is, however, very
 * unlikely, since even 1000 bytes can fit 37 events containing
 * ~27 bytes each, which means only happens upon a failure of 37
 * instances. In such a case event loss is the mildest problem to
 * deal with.
 */
static void
swim_decrease_event_ttd(struct swim *swim)
{
	struct swim_member *member, *tmp;
	rlist_foreach_entry_safe(member, &swim->dissemination_queue,
				 in_dissemination_queue,
				 tmp) {
		if (member->payload_ttd > 0) {
			if (--member->payload_ttd == 0)
				swim_cached_round_msg_invalidate(swim);
		}
		if (--member->status_ttd == 0) {
			rlist_del_entry(member, in_dissemination_queue);
			swim_cached_round_msg_invalidate(swim);
			if (member->status == MEMBER_LEFT)
				swim_delete_member(swim, member);
		}
	}
}

/**
 * Once per specified timeout trigger a next round step. In round
 * step a next member is taken from the round queue and a round
 * message is sent to him. One member per step.
 */
static void
swim_begin_step(struct ev_loop *loop, struct ev_timer *t, int events)
{
	assert((events & EV_TIMER) != 0);
	(void) events;
	(void) loop;
	struct swim *swim = (struct swim *) t->data;
	if (! rlist_empty(&swim->round_queue))
		say_verbose("SWIM %d: continue the round", swim_fd(swim));
	else
		swim_new_round(swim);
	/*
	 * Possibly empty, if no members but self are specified.
	 */
	if (rlist_empty(&swim->round_queue))
		return;
	swim_encode_round_msg(swim);
	struct swim_member *m =
		rlist_first_entry(&swim->round_queue, struct swim_member,
				  in_round_queue);
	swim_task_send(&swim->round_step_task, &m->addr, &swim->scheduler);
}

/**
 * After a round message is sent, the addressee can be popped from
 * the queue, and the next step is scheduled.
 */
static void
swim_complete_step(struct swim_task *task,
		   struct swim_scheduler *scheduler, int rc)
{
	(void) rc;
	(void) task;
	struct swim *swim = swim_by_scheduler(scheduler);
	swim_ev_timer_again(loop(), &swim->round_tick);
	/*
	 * It is possible that the original member was deleted
	 * manually during the task execution.
	 */
	if (rlist_empty(&swim->round_queue))
		return;
	struct swim_member *m =
		rlist_first_entry(&swim->round_queue, struct swim_member,
				  in_round_queue);
	if (swim_inaddr_eq(&m->addr, &task->dst)) {
		rlist_shift(&swim->round_queue);
		if (rc > 0) {
			/*
			 * Each round message contains
			 * dissemination and failure detection
			 * sections.
			 */
			swim_wait_ack(swim, m, false);
			swim_decrease_event_ttd(swim);
		}
	}
}

/** Schedule send of a failure detection message. */
static void
swim_send_fd_msg(struct swim *swim, struct swim_task *task,
		 const struct sockaddr_in *dst, enum swim_fd_msg_type type,
		 const struct sockaddr_in *proxy)
{
	/*
	 * Reset packet allocator in case if task is being reused.
	 */
	assert(! swim_task_is_scheduled(task));
	swim_packet_create(&task->packet);
	if (proxy != NULL)
		swim_task_set_proxy(task, proxy);
	char *header = swim_packet_alloc(&task->packet, 1);
	int map_size = swim_encode_src_uuid(swim, &task->packet);
	map_size += swim_encode_failure_detection(swim, &task->packet, type);
	assert(map_size == 2);
	mp_encode_map(header, map_size);
	say_verbose("SWIM %d: schedule %s to %s", swim_fd(swim),
		    swim_fd_msg_type_strs[type], swim_inaddr_str(dst));
	swim_task_send(task, dst, &swim->scheduler);
}

/** Schedule send of an ack. */
static inline void
swim_send_ack(struct swim *swim, struct swim_task *task,
	      const struct sockaddr_in *dst)
{
	swim_send_fd_msg(swim, task, dst, SWIM_FD_MSG_ACK, NULL);
}

/**
 * Schedule an indirect ack through @a proxy. Indirect ACK is sent
 * only when this instance receives an indirect ping. It means
 * that another member tries to reach this one via other nodes,
 * and inexplicably failed to do it directly.
 */
static inline int
swim_send_indirect_ack(struct swim *swim, const struct sockaddr_in *dst,
		       const struct sockaddr_in *proxy)
{
	struct swim_task *task =
		swim_task_new(swim_task_delete_cb, swim_task_delete_cb,
			      "indirect ack");
	if (task == NULL)
		return -1;
	swim_send_fd_msg(swim, task, dst, SWIM_FD_MSG_ACK, proxy);
	return 0;
}

/** Schedule send of a ping. */
static inline void
swim_send_ping(struct swim *swim, struct swim_task *task,
	       const struct sockaddr_in *dst)
{
	swim_send_fd_msg(swim, task, dst, SWIM_FD_MSG_PING, NULL);
}

/** Indirect ping task completion callback. */
static void
swim_iping_task_complete(struct swim_task *task,
			 struct swim_scheduler *scheduler, int rc)
{
	if (rc < 0)
		goto finish;
	struct swim *swim = swim_by_scheduler(scheduler);
	struct swim_member *m = swim_find_member(swim, &task->uuid);
	/*
	 * A member can be already removed, probably manually, so
	 * check for NULL. Additionally it is possible that before
	 * this indirect ping managed to get EV_WRITE, already an
	 * ACK was received and the member is alive again. Then
	 * nothing to do.
	 */
	if (m != NULL && m->status != MEMBER_ALIVE)
		swim_wait_ack(swim, m, true);
finish:
	swim_task_delete_cb(task, scheduler, rc);
}

/**
 * Schedule a number of indirect pings to a member @a dst.
 * Indirect pings are used when direct pings are not acked too
 * long. The SWIM paper explains that it is a protection against
 * false-positive failure detection when a node sends ACKs too
 * slow, or the network is in trouble. Then other nodes can try to
 * access it via different channels and members. The algorithm is
 * simple - choose a fixed number of random members and send pings
 * to the suspected member via them in parallel.
 */
static inline int
swim_send_indirect_pings(struct swim *swim, const struct swim_member *dst)
{
	struct mh_swim_table_t *t = swim->members;
	int member_count = mh_size(t);
	int rnd = swim_scaled_rand(0, member_count - 1);
	mh_int_t rc = mh_swim_table_random(t, rnd), end = mh_end(t);
	for (int member_i = 0, task_i = 0; member_i < member_count &&
	     task_i < INDIRECT_PING_COUNT; ++member_i) {
		struct swim_member *m = *mh_swim_table_node(t, rc);
		/*
		 * It makes no sense to send an indirect ping via
		 * self and via destination - it would be just
		 * direct ping then.
		 */
		if (m != swim->self && !swim_inaddr_eq(&dst->addr, &m->addr)) {
			struct swim_task *t =
				swim_task_new(swim_iping_task_complete,
					      swim_task_delete_cb,
					      "indirect ping");
			if (t == NULL)
				return -1;
			t->uuid = dst->uuid;
			swim_task_set_proxy(t, &m->addr);
			swim_send_fd_msg(swim, t, &dst->addr, SWIM_FD_MSG_PING,
					 &m->addr);
		}
		/*
		 * First random member could be chosen too close
		 * to the hash end. Here the cycle is wrapped.
		 */
		rc = mh_next(t, rc);
		if (rc == end)
			rc = mh_first(t);
	}
	return 0;
}

/**
 * Check for unacknowledged pings. A ping is unacknowledged if an
 * ack was not received during ack timeout. An unacknowledged ping
 * is resent here.
 */
static void
swim_check_acks(struct ev_loop *loop, struct ev_timer *t, int events)
{
	assert((events & EV_TIMER) != 0);
	(void) events;
	struct swim *swim = (struct swim *) t->data;
	double current_time = swim_time();
	struct swim_member *m;
	while ((m = wait_ack_heap_top(&swim->wait_ack_heap)) != NULL) {
		if (current_time < m->ping_deadline) {
			swim_ev_timer_again(loop, t);
			return;
		}
		wait_ack_heap_pop(&swim->wait_ack_heap);
		++m->unacknowledged_pings;
		switch (m->status) {
		case MEMBER_ALIVE:
			if (m->unacknowledged_pings < NO_ACKS_TO_SUSPECT)
				break;
			m->status = MEMBER_SUSPECTED;
			swim_on_member_update(swim, m);
			if (swim_send_indirect_pings(swim, m) != 0)
				diag_log();
			break;
		case MEMBER_SUSPECTED:
			if (m->unacknowledged_pings >= NO_ACKS_TO_DEAD) {
				m->status = MEMBER_DEAD;
				swim_on_member_update(swim, m);
			}
			break;
		case MEMBER_DEAD:
			if (m->unacknowledged_pings >= NO_ACKS_TO_GC &&
			    swim->gc_mode == SWIM_GC_ON && m->status_ttd == 0) {
				swim_delete_member(swim, m);
				continue;
			}
			break;
		case MEMBER_LEFT:
			continue;
		default:
			unreachable();
		}
		swim_send_ping(swim, &m->ping_task, &m->addr);
	}
}

/** Update member's address.*/
static inline void
swim_update_member_addr(struct swim *swim, struct swim_member *member,
			const struct sockaddr_in *addr)
{
	assert(! swim_inaddr_eq(&member->addr, addr));
	member->addr = *addr;
	swim_on_member_update(swim, member);
}

/**
 * Update an existing member with a new definition. It is expected
 * that @a def has an incarnation not older that @a member has.
 */
static inline void
swim_update_member(struct swim *swim, const struct swim_member_def *def,
		   struct swim_member *member)
{
	assert(member != swim->self);
	assert(def->incarnation >= member->incarnation);
	/*
	 * Payload update rules are simple: it can be updated
	 * either if the new payload has a bigger incarnation, or
	 * the same incarnation, but local payload is outdated.
	 */
	bool update_payload = false;
	if (def->incarnation > member->incarnation) {
		if (! swim_inaddr_eq(&def->addr, &member->addr))
			swim_update_member_addr(swim, member, &def->addr);
		if (def->payload_size >= 0) {
			update_payload = true;
		} else if (member->is_payload_up_to_date) {
			member->is_payload_up_to_date = false;
			swim_on_member_update(swim, member);
		}
	} else if (! member->is_payload_up_to_date && def->payload_size >= 0) {
		update_payload = true;
	}
	if (update_payload &&
	    swim_update_member_payload(swim, member, def->payload,
				       def->payload_size) != 0) {
		/* Not such a critical error. */
		diag_log();
	}
	swim_update_member_inc_status(swim, member, def->status,
				      def->incarnation);
}

/**
 * Update or create a member by its definition, received from a
 * remote instance.
 * @param swim SWIM instance to upsert into.
 * @param def Member definition to build a new member or update an
 *        existing one.
 * @param[out] result A result member: a new, or an updated, or
 *        NULL in case of nothing has changed. For example, @a def
 *        was too old.
 *
 * @retval 0 Success. Member is added, or updated. Or nothing has
 *         changed but not always it is an error.
 * @retval -1 Error.
 */
static int
swim_upsert_member(struct swim *swim, const struct swim_member_def *def,
		   struct swim_member **result)
{
	struct swim_member *member = swim_find_member(swim, &def->uuid);
	if (member == NULL) {
		if (def->status == MEMBER_LEFT ||
		    (def->status == MEMBER_DEAD &&
		     swim->gc_mode == SWIM_GC_ON)) {
			/*
			 * Do not 'resurrect' dead members to
			 * prevent 'ghost' members. Ghost member
			 * is a one declared as dead, sent via
			 * anti-entropy, and removed from local
			 * member table, but then returned back
			 * from received anti-entropy, as again
			 * dead. Such dead members could 'live'
			 * forever.
			 */
			goto skip;
		}
		*result = swim_new_member(swim, &def->addr, &def->uuid,
					  def->status, def->incarnation,
					  def->payload, def->payload_size);
		return *result != NULL ? 0 : -1;
	}
	*result = member;
	struct swim_member *self = swim->self;
	if (member != self) {
		if (def->incarnation < member->incarnation)
			goto skip;
		swim_update_member(swim, def, member);
		return 0;
	}
	/*
	 * It is possible that other instances know a bigger
	 * incarnation of this instance - such thing happens when
	 * the instance restarts and loses its local incarnation
	 * number. It will be restored by receiving dissemination
	 * and anti-entropy messages about self.
	 */
	if (self->incarnation < def->incarnation) {
		self->incarnation = def->incarnation;
		swim_on_member_update(swim, self);
	}
	if (def->status != MEMBER_ALIVE &&
	    def->incarnation == self->incarnation) {
		/*
		 * In the cluster a gossip exists that this
		 * instance is not alive. Refute this information
		 * with a bigger incarnation.
		 */
		self->incarnation++;
		swim_on_member_update(swim, self);
	}
	return 0;
skip:
	*result = NULL;
	return 0;
}

/**
 * Decode a bunch of members encoded as a MessagePack array. Each
 * correctly decoded member is upserted into the member table.
 */
static int
swim_process_members(struct swim *swim, const char *prefix,
		     const char **pos, const char *end)
{
	uint32_t size;
	if (swim_decode_array(pos, end, &size, prefix, "root") != 0)
		return -1;
	for (uint64_t i = 0; i < size; ++i) {
		struct swim_member_def def;
		struct swim_member *member;
		if (swim_member_def_decode(&def, pos, end, prefix) != 0)
			return -1;
		if (swim_upsert_member(swim, &def, &member) != 0) {
			/*
			 * Not a critical error. Other members
			 * still can be updated.
			 */
			diag_log();
		}
	}
	return 0;
}

/** Decode an anti-entropy message, update member table. */
static int
swim_process_anti_entropy(struct swim *swim, const char **pos, const char *end)
{
	say_verbose("SWIM %d: process anti-entropy", swim_fd(swim));
	const char *prefix = "invalid anti-entropy message:";
	return swim_process_members(swim, prefix, pos, end);
}

/**
 * Decode a failure detection message. Schedule acks, process
 * acks.
 */
static int
swim_process_failure_detection(struct swim *swim, const char **pos,
			       const char *end, const struct sockaddr_in *src,
			       const struct tt_uuid *uuid,
			       const struct sockaddr_in *proxy)
{
	const char *prefix = "invalid failure detection message:";
	struct swim_failure_detection_def def;
	struct swim_member_def mdef;
	if (swim_failure_detection_def_decode(&def, pos, end, prefix) != 0)
		return -1;
	say_verbose("SWIM %d: process failure detection's %s", swim_fd(swim),
		    swim_fd_msg_type_strs[def.type]);
	swim_member_def_create(&mdef);
	mdef.addr = *src;
	mdef.incarnation = def.incarnation;
	mdef.uuid = *uuid;
	struct swim_member *member;
	if (swim_upsert_member(swim, &mdef, &member) != 0)
		return -1;
	/*
	 * It can be NULL, for example, in case of too old
	 * incarnation of the failure detection request. For the
	 * obvious reasons we do not use outdated ACKs. But we
	 * also ignore outdated pings. It is because 1) we need to
	 * be consistent in neglect of all old messages; 2) if a
	 * ping is considered old, then after it was sent, this
	 * SWIM instance has already interacted with the sender
	 * and learned its new incarnation.
	 */
	if (member == NULL)
		return 0;
	/*
	 * It is well known fact, that SWIM compares statuses as
	 * compound keys {incarnation, status}. If inc1 == inc2,
	 * but status1 > status2, nothing should happen. But it
	 * works for anti-entropy only, when the new status is
	 * received indirectly, as a gossip. Here is a different
	 * case - this message was received from the member
	 * directly, and evidently it is alive.
	 */
	if (def.incarnation == member->incarnation &&
	    member->status != MEMBER_ALIVE) {
		member->status = MEMBER_ALIVE;
		swim_on_member_update(swim, member);
	}

	switch (def.type) {
	case SWIM_FD_MSG_PING:
		if (proxy != NULL) {
			if (swim_send_indirect_ack(swim, &member->addr,
						   proxy) != 0)
				diag_log();
		} else if (! swim_task_is_scheduled(&member->ack_task)) {
			swim_send_ack(swim, &member->ack_task, &member->addr);
		}
		break;
	case SWIM_FD_MSG_ACK:
		member->unacknowledged_pings = 0;
		if (! heap_node_is_stray(&member->in_wait_ack_heap))
			wait_ack_heap_delete(&swim->wait_ack_heap, member);
		break;
	default:
		unreachable();
	}
	return 0;
}

/**
 * Decode a dissemination message. Schedule new events, update
 * members.
 */
static int
swim_process_dissemination(struct swim *swim, const char **pos, const char *end)
{
	say_verbose("SWIM %d: process dissemination", swim_fd(swim));
	const char *prefix = "invalid dissemination message:";
	return swim_process_members(swim, prefix, pos, end);
}

/**
 * Decode a quit message. Schedule dissemination, change status.
 */
static int
swim_process_quit(struct swim *swim, const char **pos, const char *end,
		  const struct tt_uuid *uuid)
{
	say_verbose("SWIM %d: process quit", swim_fd(swim));
	const char *prefix = "invalid quit message:";
	uint32_t size;
	if (swim_decode_map(pos, end, &size, prefix, "root") != 0)
		return -1;
	if (size != 1) {
		diag_set(SwimError, "%s map of size 1 is expected", prefix);
		return -1;
	}
	uint64_t tmp;
	if (swim_decode_uint(pos, end, &tmp, prefix, "a key") != 0)
		return -1;
	if (tmp != SWIM_QUIT_INCARNATION) {
		diag_set(SwimError, "%s a key should be incarnation", prefix);
		return -1;
	}
	if (swim_decode_uint(pos, end, &tmp, prefix, "incarnation") != 0)
		return -1;
	struct swim_member *m = swim_find_member(swim, uuid);
	if (m == NULL)
		return 0;
	/*
	 * Check for 'self' in case this instance took UUID of a
	 * quited instance.
	 */
	if (m != swim->self) {
		swim_update_member_inc_status(swim, m, MEMBER_LEFT, tmp);
	} else if (tmp >= m->incarnation) {
		m->incarnation = tmp + 1;
		swim_on_member_update(swim, m);
	}
	return 0;
}

/** Process a new message. */
static void
swim_on_input(struct swim_scheduler *scheduler, const char *pos,
	      const char *end, const struct sockaddr_in *src,
	      const struct sockaddr_in *proxy)
{
	const char *prefix = "invalid message:";
	struct swim *swim = swim_by_scheduler(scheduler);
	struct tt_uuid uuid;
	uint32_t size;
	if (swim_decode_map(&pos, end, &size, prefix, "root") != 0)
		goto error;
	if (size == 0) {
		diag_set(SwimError, "%s body can not be empty", prefix);
		goto error;
	}
	uint64_t key;
	if (swim_decode_uint(&pos, end, &key, prefix, "a key") != 0)
		goto error;
	if (key != SWIM_SRC_UUID) {
		diag_set(SwimError, "%s first key should be source UUID",
			 prefix);
		goto error;
	}
	if (swim_decode_uuid(&uuid, &pos, end, prefix, "source uuid") != 0)
		goto error;
	--size;
	for (uint32_t i = 0; i < size; ++i) {
		if (swim_decode_uint(&pos, end, &key, prefix, "a key") != 0)
			goto error;
		switch(key) {
		case SWIM_ANTI_ENTROPY:
			if (swim_process_anti_entropy(swim, &pos, end) != 0)
				goto error;
			break;
		case SWIM_FAILURE_DETECTION:
			if (swim_process_failure_detection(swim, &pos, end,
							   src, &uuid,
							   proxy) != 0)
				goto error;
			break;
		case SWIM_DISSEMINATION:
			if (swim_process_dissemination(swim, &pos, end) != 0)
				goto error;
			break;
		case SWIM_QUIT:
			if (swim_process_quit(swim, &pos, end, &uuid) != 0)
				goto error;
			break;
		default:
			diag_set(SwimError, "%s unexpected key", prefix);
			goto error;
		}
	}
	return;
error:
	diag_log();
}

struct swim *
swim_new(void)
{
	struct swim *swim = (struct swim *) calloc(1, sizeof(*swim));
	if (swim == NULL) {
		diag_set(OutOfMemory, sizeof(*swim), "calloc", "swim");
		return NULL;
	}
	swim->members = mh_swim_table_new();
	if (swim->members == NULL) {
		free(swim);
		diag_set(OutOfMemory, sizeof(*swim->members),
			 "mh_swim_table_new", "members");
		return NULL;
	}
	rlist_create(&swim->round_queue);
	swim_ev_timer_init(&swim->round_tick, swim_begin_step,
			   0, HEARTBEAT_RATE_DEFAULT);
	swim->round_tick.data = (void *) swim;
	swim_task_create(&swim->round_step_task, swim_complete_step, NULL,
			 "round packet");
	swim_scheduler_create(&swim->scheduler, swim_on_input);

	/* Failure detection component. */
	wait_ack_heap_create(&swim->wait_ack_heap);
	swim_ev_timer_init(&swim->wait_ack_tick, swim_check_acks,
			   0, ACK_TIMEOUT_DEFAULT);
	swim->wait_ack_tick.data = (void *) swim;
	swim->gc_mode = SWIM_GC_ON;

	/* Dissemination component. */
	rlist_create(&swim->dissemination_queue);

	return swim;
}

/**
 * Parse URI, filter out everything but IP addresses and ports,
 * and fill a struct sockaddr_in.
 */
static inline int
swim_uri_to_addr(const char *uri, struct sockaddr_in *addr,
		 const char *prefix)
{
	struct sockaddr_storage storage;
	bool is_host_empty;
	if (sio_uri_to_addr(uri, (struct sockaddr *) &storage,
			    &is_host_empty) != 0)
		return -1;
	if (storage.ss_family != AF_INET) {
		diag_set(IllegalParams, "%s only IP sockets are supported",
			 prefix);
		return -1;
	}
	*addr = *((struct sockaddr_in *) &storage);
	if (is_host_empty) {
		/*
		 * This condition is satisfied when host is
		 * omitted and URI is "port". Note, that
		 * traditionally "port" is converted to
		 * "0.0.0.0:port" what means binding to all the
		 * interfaces simultaneously, but it would not
		 * work for SWIM. There is why:
		 *
		 *     - Different instances interacting with this
		 *       one via not the same interface would see
		 *       different source IP addresses. It would
		 *       mess member tables;
		 *
		 *     - This instance would not be able to encode
		 *       its IP address in the meta section,
		 *       because it has no a fixed IP. At the same
		 *       time omission of it and usage of UDP
		 *       header source address is not possible as
		 *       well, because UDP header is not encrypted
		 *       and therefore is not safe to look at.
		 */
		addr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	} else if (addr->sin_addr.s_addr == 0) {
		diag_set(IllegalParams, "%s INADDR_ANY is not supported",
			 prefix);
		return -1;
	}
	return 0;
}

int
swim_cfg(struct swim *swim, const char *uri, double heartbeat_rate,
	 double ack_timeout, enum swim_gc_mode gc_mode,
	 const struct tt_uuid *uuid)
{
	const char *prefix = "swim.cfg:";
	struct sockaddr_in addr;
	if (uri != NULL && swim_uri_to_addr(uri, &addr, prefix) != 0)
		return -1;
	bool is_first_cfg = swim->self == NULL;
	struct swim_member *new_self = NULL;
	if (is_first_cfg) {
		if (uuid == NULL || tt_uuid_is_nil(uuid) || uri == NULL) {
			diag_set(SwimError, "%s UUID and URI are mandatory in "\
				 "a first config", prefix);
			return -1;
		}
		swim->self = swim_new_member(swim, &addr, uuid, MEMBER_ALIVE,
					     0, NULL, 0);
		if (swim->self == NULL)
			return -1;
	} else if (uuid == NULL || tt_uuid_is_nil(uuid)) {
		uuid = &swim->self->uuid;
	} else if (! tt_uuid_is_equal(uuid, &swim->self->uuid)) {
		if (swim_find_member(swim, uuid) != NULL) {
			diag_set(SwimError, "%s a member with such UUID "\
				 "already exists", prefix);
			return -1;
		}
		new_self = swim_new_member(swim, &swim->self->addr, uuid,
					   MEMBER_ALIVE, 0, swim->self->payload,
					   swim->self->payload_size);
		if (new_self == NULL)
			return -1;
	}
	if (uri != NULL) {
		/*
		 * Bind is smart - it does nothing if the address
		 * was not changed.
		 */
		if (swim_scheduler_bind(&swim->scheduler, &addr) != 0) {
			if (is_first_cfg) {
				swim_delete_member(swim, swim->self);
				swim->self = NULL;
			} else if (new_self != NULL) {
				swim_delete_member(swim, new_self);
			}
			return -1;
		}
		/*
		 * A real address can be different from a one
		 * passed by user. For example, if 0 port was
		 * specified.
		 */
		addr = swim->scheduler.transport.addr;
	} else {
		addr = swim->self->addr;
	}
	struct ev_timer *t = &swim->round_tick;
	if (t->repeat != heartbeat_rate && heartbeat_rate > 0) {
		swim_ev_timer_set(t, 0, heartbeat_rate);
		if (swim_ev_is_active(t))
			swim_ev_timer_again(loop(), t);
	}
	t = &swim->wait_ack_tick;
	if (t->repeat != ack_timeout && ack_timeout > 0) {
		swim_ev_timer_set(t, 0, ack_timeout);
		if (swim_ev_is_active(t))
			swim_ev_timer_again(loop(), t);
	}

	if (new_self != NULL) {
		swim->self->status = MEMBER_LEFT;
		swim_on_member_update(swim, swim->self);
		swim->self = new_self;
	}
	if (! swim_inaddr_eq(&addr, &swim->self->addr)) {
		swim->self->incarnation++;
		swim_update_member_addr(swim, swim->self, &addr);
	}
	if (gc_mode != SWIM_GC_DEFAULT)
		swim->gc_mode = gc_mode;
	return 0;
}

int
swim_set_codec(struct swim *swim, enum crypto_algo algo, enum crypto_mode mode,
	       const char *key, int key_size)
{
	return swim_scheduler_set_codec(&swim->scheduler, algo, mode,
					key, key_size);
}

bool
swim_is_configured(const struct swim *swim)
{
	return swim->self != NULL;
}

int
swim_set_payload(struct swim *swim, const char *payload, int payload_size)
{
	if (payload_size > MAX_PAYLOAD_SIZE || payload_size < 0) {
		diag_set(IllegalParams, "Payload should be <= %d and >= 0",
			 MAX_PAYLOAD_SIZE);
		return -1;
	}
	struct swim_member *self = swim->self;
	if (swim_update_member_payload(swim, self, payload, payload_size) != 0)
		return -1;
	self->incarnation++;
	swim_on_member_update(swim, self);
	return 0;
}

int
swim_add_member(struct swim *swim, const char *uri, const struct tt_uuid *uuid)
{
	const char *prefix = "swim.add_member:";
	assert(swim_is_configured(swim));
	if (uri == NULL || uuid == NULL || tt_uuid_is_nil(uuid)) {
		diag_set(SwimError, "%s URI and UUID are mandatory", prefix);
		return -1;
	}
	struct sockaddr_in addr;
	if (swim_uri_to_addr(uri, &addr, prefix) != 0)
		return -1;
	struct swim_member *member = swim_find_member(swim, uuid);
	if (member == NULL) {
		member = swim_new_member(swim, &addr, uuid, MEMBER_ALIVE, 0,
					 NULL, -1);
		return member == NULL ? -1 : 0;
	}
	diag_set(SwimError, "%s a member with such UUID already exists",
		 prefix);
	return -1;
}

int
swim_remove_member(struct swim *swim, const struct tt_uuid *uuid)
{
	assert(swim_is_configured(swim));
	const char *prefix = "swim.remove_member:";
	if (uuid == NULL || tt_uuid_is_nil(uuid)) {
		diag_set(SwimError, "%s UUID is mandatory", prefix);
		return -1;
	}
	struct swim_member *member = swim_find_member(swim, uuid);
	if (member == NULL)
		return 0;
	if (member == swim->self) {
		diag_set(SwimError, "%s can not remove self", prefix);
		return -1;
	}
	swim_delete_member(swim, member);
	return 0;
}

int
swim_probe_member(struct swim *swim, const char *uri)
{
	assert(swim_is_configured(swim));
	if (uri == NULL) {
		diag_set(SwimError, "swim.probe_member: URI is mandatory");
		return -1;
	}
	struct sockaddr_in addr;
	if (swim_uri_to_addr(uri, &addr, "swim.probe_member:") != 0)
		return -1;
	struct swim_task *t = swim_task_new(swim_task_delete_cb,
					    swim_task_delete_cb, "probe ping");
	if (t == NULL)
		return -1;
	swim_send_ping(swim, t, &addr);
	return 0;
}

int
swim_broadcast(struct swim *swim, int port)
{
	assert(swim_is_configured(swim));
	if (port < 0)
		port = ntohs(swim->self->addr.sin_port);
	struct swim_bcast_task *t = swim_bcast_task_new(port, "broadcast ping");
	if (t == NULL)
		return -1;
	swim_send_ping(swim, &t->base, &t->base.dst);
	return 0;
}

int
swim_size(const struct swim *swim)
{
	return mh_size(swim->members);
}

void
swim_delete(struct swim *swim)
{
	swim_scheduler_destroy(&swim->scheduler);
	swim_ev_timer_stop(loop(), &swim->round_tick);
	swim_ev_timer_stop(loop(), &swim->wait_ack_tick);
	mh_int_t node;
	mh_foreach(swim->members, node) {
		struct swim_member *m =
			*mh_swim_table_node(swim->members, node);
		rlist_del_entry(m, in_round_queue);
		if (! heap_node_is_stray(&m->in_wait_ack_heap))
			wait_ack_heap_delete(&swim->wait_ack_heap, m);
		rlist_del_entry(m, in_dissemination_queue);
		swim_member_delete(m);
	}
	/*
	 * Destroy the task after members - otherwise they would
	 * try to invalidate the already destroyed task.
	 */
	swim_task_destroy(&swim->round_step_task);
	wait_ack_heap_destroy(&swim->wait_ack_heap);
	mh_swim_table_delete(swim->members);
	free(swim->shuffled);
	free(swim);
}

/**
 * Quit message is broadcasted in the same way as round messages,
 * step by step, with the only difference that quit round steps
 * follow each other without delays.
 */
static void
swim_quit_step_complete(struct swim_task *task,
			struct swim_scheduler *scheduler, int rc)
{
	(void) rc;
	struct swim *swim = swim_by_scheduler(scheduler);
	if (rlist_empty(&swim->round_queue)) {
		swim_delete(swim);
		return;
	}
	struct swim_member *m =
		rlist_shift_entry(&swim->round_queue, struct swim_member,
				  in_round_queue);
	swim_task_send(task, &m->addr, scheduler);
}

/**
 * Encode 'quit' command.
 * @retval Number of key-values added to the packet's root map.
 */
static inline int
swim_encode_quit(struct swim *swim, struct swim_packet *packet)
{
	struct swim_quit_bin bin;
	char *pos = swim_packet_alloc(packet, sizeof(bin));
	if (pos == NULL)
		return 0;
	swim_quit_bin_create(&bin, swim->self->incarnation);
	memcpy(pos, &bin, sizeof(bin));
	return 1;
}

void
swim_quit(struct swim *swim)
{
	assert(swim_is_configured(swim));
	swim_ev_timer_stop(loop(), &swim->round_tick);
	swim_ev_timer_stop(loop(), &swim->wait_ack_tick);
	swim_scheduler_stop_input(&swim->scheduler);
	/* Start the last round - quiting. */
	swim_new_round(swim);
	struct swim_task *task = &swim->round_step_task;
	swim_task_destroy(task);
	swim_task_create(task, swim_quit_step_complete, swim_task_delete_cb,
			 "quit");
	char *header = swim_packet_alloc(&task->packet, 1);
	int rc = swim_encode_src_uuid(swim, &task->packet) +
		 swim_encode_quit(swim, &task->packet);
	assert(rc == 2);
	mp_encode_map(header, rc);
	swim->self->status = MEMBER_LEFT;
	swim_quit_step_complete(task, &swim->scheduler, 0);
}

struct swim_member *
swim_self(struct swim *swim)
{
	assert(swim_is_configured(swim));
	return swim->self;
}

struct swim_member *
swim_member_by_uuid(struct swim *swim, const struct tt_uuid *uuid)
{
	assert(swim_is_configured(swim));
	return swim_find_member(swim, uuid);
}

enum swim_member_status
swim_member_status(const struct swim_member *member)
{
	return member->status;
}

struct swim_iterator *
swim_iterator_open(struct swim *swim)
{
	assert(swim_is_configured(swim));
	swim->iterator = mh_first(swim->members);
	return (struct swim_iterator *) swim;
}

struct swim_member *
swim_iterator_next(struct swim_iterator *iterator)
{
	struct swim *swim = (struct swim *) iterator;
	assert(swim_is_configured(swim));
	struct mh_swim_table_t *t = swim->members;
	if (swim->iterator == mh_end(t))
		return NULL;
	struct swim_member *m = *mh_swim_table_node(t, swim->iterator);
	swim->iterator = mh_next(t, swim->iterator);
	return m;
}

void
swim_iterator_close(struct swim_iterator *iterator)
{
	(void) iterator;
}

const char *
swim_member_uri(const struct swim_member *member)
{
	return swim_inaddr_str(&member->addr);
}

const struct tt_uuid *
swim_member_uuid(const struct swim_member *member)
{
	return &member->uuid;
}

uint64_t
swim_member_incarnation(const struct swim_member *member)
{
	return member->incarnation;
}

const char *
swim_member_payload(const struct swim_member *member, int *size)
{
	*size = member->payload_size;
	return member->payload;
}

bool
swim_member_is_payload_up_to_date(const struct swim_member *member)
{
	return member->is_payload_up_to_date;
}
